# Audit: is the remake identical to the original?

Status: commit 4aa985f (v0.0.50 plus the cleanup pass), audit of 2026-09-30. Nothing in this list has been changed yet: it is the list of what differs, in the
order in which it is proposed to be fixed. The owner decides (AGENTS.md rule 9).

## 0. Progress (updated with every release)
* v0.0.51: shortcuts that the original did not have removed (owner request), hit-point numbers on by default (owner tweak).
* v0.0.52: batch 1 item 1 (hill queue: LH NEW-1, NEW-2, NEW-3), reported by the owner in play ("six ants sent to the base, three cancelled their queue").
* v0.0.53: batch 1 items 2 and 3 (ability orders re-issued when the approach tile is taken: LM NEW-M1; group re-click rules: LM NEW-M2; the invented left-column rule next to the hill removed: LB NEW-3).
* v0.0.54: batch 1 items 4 and 5 (bridge timer kept through an interrupted demolish, timeout only on a completed bridge: LB NEW-1; swimmer re-enters its action when its bridge collapses, silent collapse: LB NEW-2, LE NEW-5, LS NEW-9; scores never clamped, signed loot: LF NEW-4).
* v0.0.55: batch 1 items 6 and 8 (dropped teams: raid refused, special tiles ordinary, no power-ups, ally pedestal live count: LF NEW-5, LH NEW-5 / NEW-6, LU NEW-8, LK NEW-4; hatch retry 1000 ms: LX).
* v0.0.56: batch 1 item 7 (flower droppers: 3 s poll, stamp at the posting, tile test, 820 ms effect, type formula: LB NEW-4, LE R10, LX FDTASK).
* v0.0.57: batch 1 item 9, the small items that could be verified (level-start facing: LH NEW-4; power-up drop tile test: LB NEW-5; bsputter / dsplash order: LE NEW-2). **Left open on purpose**: stun and a queued path (LM NEW-M3), the `occ_scan` team order (LM NEW-M6 / LK NEW-8: needs proof of the slot-to-player mapping), the invented counters (LB NEW-6, goes with the cleanup), burnout / bridge tasks as (tile, deadline) entries with the 2500 ms poll (LE NEW-4).
* v0.0.58: batch 2 part 1 (one sting and the music cut at once: LS NEW-1, LR NEW-1/NEW-2 (music), LF NEW-2; global cues canthatch / anthill alarm and owner-only powerupd: LS NEW-2/NEW-3, LF NEW-1, LE C10; the click of an order: LI NEW-3).
* v0.0.59: batch 2 part 2 (the sound law: radius 2500, Chebyshev, dB, far-channel pan, the Sound Volume option in every sound: LS NEW-6, LX; the Sound / Music sliders apply at the release, test voice: LS NEW-5, LR R2.1d).
* v0.0.60: batch 2 part 3 (tracked sounds are cut when a clip is replaced or the sprite is removed: LS NEW-4, LK V-K4; a pressed button's click is cut at the release).
* v0.0.61: batch 2 part 4 (music: the intro plays once and random pieces chain, focus loss closes it and focus gain starts a new piece, slider release restarts, match end closes at once: LS NEW-8, LR NEW-2). **Batch 2 is done.** Not done on purpose: the start-up jingle (vendor artwork, owner decision), the 32-channel limit (by necessity).
* v0.0.62: batch 3 part 1 (the end rules: nobody left, a strictly best allied survivor, the drop-out win test, quitting as a forfeit with the quitter's row last; `Quit` command, protocol version 4: LR R1.4c, LS S-4.T3, LX #1). Next: the results screen rebuild.
* v0.0.63: batch 3 part 2 (the results screen rebuilt: waiting label and 250 ms, rows per alliance with the original's sort and places, left-aligned numbers, animated portraits, one cue when the rows appear, Enter / C / Q / X leave, Esc does nothing: LR R1.1 - R1.4b, LF NEW-6, LU S-08 / S-14). **Batch 3 is done.** Not changed on purpose: the web "Leave" (owner decision), the counters' wrap (substitute font).
* v0.0.64: batch 4 part 1 (the setup screen: searched map list instead of a built-in one, button class at the release, the exact key map, 500 ms refresh, invented click targets removed: LR R3.1 - R3.4, NEW-5; community maps: loader accepts trailing bytes, rows-first header, egg stock = final word). Still open in batch 4: options (sliders, quick-chat fields, persistence), the startup flow (owner decisions), the text pass.
* v0.0.65: batch 4 part 2 (the options screen: the slider class (integer model, exact hit rectangle, value on move, callback at the release), latching switches at the release, the quick chat edit fields (100 characters, focus, 150 ms caret, Enter closes, Esc nothing, no click outside), the remembered settings with the original's validity rule; LR S3, R2.x). Still open in batch 4: the startup flow (owner decisions), quick help (keys, More Help), the text pass.
* v0.0.66: batch 4 part 3 (the quick help closes only by its button at the release and its keys, Space gone at the start; the chat input box as the original's edit control: position, colour (7, 11, 15), 150 ms caret, tail; LR R4.2d, NEW-4, NEW-9). Still open in batch 4: the startup flow (owner decisions; More Help), the rest of the text pass.

## 1. What was audited and how

The request: make sure that the game is truly one-to-one with the original (improved networking aside), "check the visuals, the animations, the timing,
recheck everything".

* **Sources of truth**: `Original-Ants/Ants.exe` (Capstone disassembly; the Ghidra-style decompilation only to navigate), `Original-Ants/ants.chd`, the six
  shipped maps. The repository's own notes were treated as unverified. **The original was never run**: every statement is static evidence plus probes of the
  remake against models derived from the binary. Anything that needs the real game running is listed in section 6.
* **Earlier audits** (per area, before the releases v0.0.25 .. v0.0.43 re-derived those systems) listed deviations such as F1..F15 for the ants. They were
  never checked again. **Phase 1** of this audit re-checked every one of those findings against today's code (13 independent agents, each with its own probes
  linked against a frozen copy of the libraries) and looked for new deviations in the same area. Two areas had no earlier audit and were audited fresh: the
  original's scheduler tasks and food / economy / scoring. The per-area results are in [`audit/`](audit/) (one ledger per area, status words FIXED / PARTIAL /
  OPEN / REGRESSED / BY DECISION / WRONG, NEW for findings that no earlier report had).
* **Phase 2** (seams between systems, cross-machine cases) produced one finding before the audit started and none in Phase 1: the refusal cue plays on the
  decliner's machine too (fixed in v0.0.50). The sound audit checked every two-machine case of the original and found no second one.

## 2. What is already identical (verified)

| Area | Evidence |
|---|---|
| Ant sprites, clips, part order, team colours, mirroring | 531 clips, 9,756 clip-frames, 39,024 renders over four teams: 0 differing pixels; all 520 generated clips equal the archive; selection ears, hill brackets and the bomb art checked |
| Terrain, layer order, animation clocks, fog overlay, passability and speed classes | 1,320 full-map renders (6 maps x 220 clock values): 0 pixels; 396 fog renders; every table (1,344 tile classes, step weights, walk tables) equal; terrain animation is sampled at real time, 0 % differs |
| HUD shell, pedestals, panels, score digits, status line, dialogs | 22 panel states: 0 pixels; all 72 alliance / status strings equal; the quit, options, match-start and alliance dialogs checked |
| Movement and path finding | 1,000 random walks, 160,000 tick-boundary positions, 1,500 random A* maps, 2,533 clip cells: 0 differences against an independent model of the original; the 50 ms tick does not change positions or arrival order of unobstructed movement |
| Combat core, hill core, abilities, power-ups, food and economy | contact, strike frame, flights, death, auto-engage, enter / deposit / heal / hatch / raid, plant / defuse / ignite / bridge, pick-up window, standing on a power-up, 53 food objects, theft, lunchbox: fixed and probed; the remaining differences are listed below |
| Status texts, voices, 51 of 57 sound cues | texts and conditions equal; 16 of 21 cue call sites equal |

## 3. What differs, in the proposed order of fixing

Every item names its ledger (`L` + area letter; IDs as in `audit/ledger_*.md`) and, where known, the tests that encode the old behaviour.

### Batch 1 - rules that change outcomes (proposed v0.0.51)
1. **Hill queue** (LH NEW-1, NEW-2, NEW-3; LM probe): a carrier that is re-routed while it waits for the hill loses its queue flag, a carrier blocked on its ring
   tile is never queued, and dead ants still influence live ones. In a rush of 8 carriers only 4 deposit (the original: all 8; the original code at
   `0x101c935` / `0x101cad1` / `0x101caf2` was re-read). New tests: rush, re-path, blocked ring tile, dead ant.
2. **Ability orders when the approach tile is blocked** (LM NEW-M1): the original re-issues the special order, the remake turns it into a plain walk, so a bomber
   whose approach tile is taken plants nothing. **Group re-click rules** (LM NEW-M2): a second click on an ability target makes the ants jump back and restart.
3. **Bomb / fire-wall placement next to a hill** (LB NEW-3): the original forbids the three tiles directly above the hill (the remake has this rule, correct). The remake
   also forbids a second column of three tiles two tiles to the left of the hill (the same data read with rows and columns swapped). Remove the left column only
   (test 12.112 encodes it).
4. **Bridges** (LB NEW-1): an interrupted demolish loses the bridge's collapse timer, the bridge never collapses; swimmer on a collapsing bridge keeps the wrong clip (LB NEW-2, LE NEW-5).
5. **Scores** (LF NEW-4): the original never clamps a score at 0 (two thieves raiding a 60-point victim make it -40); the remake clamps and creates points.
6. **Dropped teams** (LF NEW-5, LH NEW-5 / NEW-6, LU NEW-8, LK NEW-4): a raid on a dropped team's hill is refused, its special tiles become ordinary ground, the ally pedestal
   counts only live teams, and its typed ants leave no power-ups.
7. **Flower droppers** (LB NEW-4, LE R10, LX): the original polls every 3 s on a fixed grid, restamps when a drop is posted, lands after 820 ms and tests "layer 2 empty or a
   power-up and no ant"; MEDIUM drops at 9 s and then every 9 s (the remake: 8 s, then every 8.8 s). Tests 12.33 and 12.126 encode the old cadence.
8. **Hatch retry** (LX, LE R13, LH H-5 was wrong): the default scheduler is the list scheduler, so the retry while the entrance is blocked is 1000 ms, not 8 ms.
9. Small: level-start ants never face north (`rand() % 7 + 1`, LH NEW-4); the power-up drop tile test (LB NEW-5); stun and a queued path (LM NEW-M3);
   `occ_scan` team order (LM NEW-M6, LK NEW-8); invented counters (LB NEW-6, LF); the burnout task keeps (tile, deadline) entries (LE NEW-4); bsputter / dsplash order (LE NEW-2).

### Batch 2 - audio (proposed v0.0.52)
1. **Match-end sting plays twice** (LR NEW-1, LS NEW-1, LF NEW-2): once from the simulation's targeted events, once from the results modal. One cue, when the rows appear.
2. **Global cues** (LS NEW-2, NEW-3, LF NEW-1, LE C10): "can't hatch", the raid alarm and the power-up-dropped cue are heard wherever the view is (or on the owner's machine only); the remake
   culls them by distance.
3. **Tracked sounds are never stopped** (LS NEW-4, LK V-K4): the original cuts a clip's sounds when the clip is replaced (bomb explosion 680 of 1144 ms, fire-ant attack 120 of 366 ms, UI clicks at
   the release). Needs an owner id per sound and stop events.
4. **Sound laws** (LS NEW-6, LX): radius 2500 Chebyshev, dB attenuation and a far-channel pan, re-applied every 500 ms; the remake uses Euclidean 800 px and equal-power pan.
5. **Order click** (LI NEW-3): frame 0 of the pedestal animation carries sound 89; every accepted order with an unlatched pedestal clicks in the original.
6. Options sound slider applies at the release and plays the test voice (LS NEW-5, LR R2.1d); music: intro once, then chained random pieces, closed on focus loss, new piece on
   return, instant cut at match end (LS NEW-8, LR NEW-2); the invented splash sound at a bridge collapse (LS NEW-9).

### Batch 3 - match end and the results screen (proposed v0.0.53)
1. **End rules (CHECKGO)** (LS S-4.T3, LX #1, LF, LR R1.4c): besides the clock, the match ends when no team has an ant, an egg or a hatch running, and when every remaining team is
   allied (the best combined score's machine ends it); a drop-out can end it; quitting is a forfeit. Today a decided match runs to the clock.
2. **Results screen rebuild** (LR R1.x, LF NEW-6, LU S-08 / S-14): rows per alliance ("A & B", summed columns, sort rule), the original's positions and left-aligned numbers,
   animated portraits, "Waiting for scores..." for at least 250 ms, keys Enter / C / Q / X, Leave exits.

### Batch 4 - setup screen, options, startup (stage R, proposed v0.0.54)
Ordered outline in `audit/ledger_screens.md` (Top 10): shared button / label / clock / config store, setup screen (alphabetical list, exact key map, every action on the
release, Drop only with a bad ping), options (slider model with the exact hit rectangles, dB curve, quick-chat Edit fields with 100 characters, persistence), startup flow
(3 s splash, at least 3 s loading, INTRO once then chained tracks, loading composite and bar), quit flow. Owner decisions first (section 4).

### Batch 5 - view and pointer (proposed v0.0.55)
1. **View origin** (LI I-06, LE NEW-8, LU C-18): the original's map view is (16, 21) 442 x 440; the remake draws it 1 px right / down and 1 px smaller (also the audio listener centre
   and the minimap marker size). Tests to rewrite: `test_app_integration.cpp:316-339`, `test_hud_layout.cpp:718-719`, the render-parity reference model.
2. **Start view** (LI NEW-1): the original scrolls the fresh view just far enough to show the square around the hill (ax - 160 .. ax + 192); the remake centres the hill (+46, +45 px off on most maps).
3. **Order feedback and voice** (LI NEW-12, verified in v0.0.53 work: `0x1027883`, `0x10289b7 .. 0x10289f6`): the pedestal pop / flash follows "at least one ant needed an order" (`FUN_010287b5` returns 1 then, even when every
   `GoTo` refuses); the voice plays only when the closest ant's `GoTo` was accepted, and a special click speaks only for a group of exactly one ant that needed an order (none for more, not even the "go" voice). The remake ties both to
   the closest ant and counts the selected ants instead of the ants that needed an order.
4. **Pointer** (LI NEW-9, I-29): full-screen exclusive, pointer polled every loop; the remake is windowed, scroll gated by a "mouse has moved" flag and the cursor sits at the centre until the
   first motion. Developer keys that do nothing in the original (Ctrl+1..4, Ctrl+C, Ctrl+Tab, Ctrl+M, ...) run before the dialog gate (LI NEW-5); Shift-select texts (NEW-2), stacked ants (NEW-4).

### Batch 6 - minimap, score boxes, chat, fog (proposed v0.0.56)
1. **Minimap** (LU NEW-3..6, LT NEW-2, LK NEW-2, LH NEW-7): in fog only power-ups, food and fire fall back to terrain (rocks, toys, hills keep their colour), no dots per object cell but flower / clover dots,
   ants at pixel position with the original's size, every ant has a dot until it is removed, allies are shown in fog, the frame is (251, 251, 255) of a fixed size, and own and allied ants that were hit in the last 5 s
   alternate between team colour and palette 250 (RGB 255, 255, 97) every 200 ms. **This last item is read in the code (`0x101a88c`, stamp at `0x1020cd1`) but the owner does not remember it in play: a screenshot of a
   fight in the original settles it.**
2. **Score boxes** (LU NEW-1, NEW-2, LF NEW-3): an allied box is half own colour, half ally colour with the summed score; absent or dropped teams are covered ("scorcovr") and slots follow the team index.
3. **Chat** (LX, LS NEW-10, LU NEW-9): scroll bar with 15 px auto-repeat and the 5 px follow animation, caret toggles every 150 ms (the remake: 750 ms), greedy wrapping by pixels, input colour (7, 11, 15).
4. **Fog reveal** (LT NEW-1, NEW-1b): the original reveals only from ant position updates (13 x 13 around the pixel tile) of the viewer and its ally; the remake also reveals a 16 x 16 box around each hill and all at once when an
   alliance forms. Layer-2 scan margin 3 instead of 5 (LT NEW-3), partially explored objects redraw from each explored body cell (LT NEW-4, LE NEW-3), food footprint at its live stage, 1 px seams at fractional window scales.

### Batch 7 - presentation of animation (proposed v0.0.57)
1. **Walking and idle frames are sampled on the 50 ms tick** (LA NEW-1, LK V-K1): the original redraws on every scheduler pass and steps at real time, so sand (40 ms frames) shows 1, 1, 1, 2 steps per tick and mud skips
   a step every sixth tick. Predict every clip (not only action clips) up to the next event frame.
2. **Death clips** (LK NEW-3): the remake still starts death1..death4 as a separate effect about 80-100 ms ahead; the original plays them on the ant's own sprite (HP digit and minimap dot stay until removal).
3. Burn overlay layer (LA F14, LE C17), fire-wall dust cloud for foreign ants (LK NEW-1, LE NEW-1), equal-y draw order (LA NEW-2), marker and digit position (LA NEW-3), the `L` toggle needs no Ctrl (LA NEW-6),
   world sprites with palette 1..31 use the viewer's HUD table (LA NEW-4, LU NEW-10).

### Batch 8 - tests and documents
* The 506 "E2E" tests exercise a private model with the old rules (4-tile knock-back, 12-tick stun, ...), not the engine (LK NEW-5); two e2e hill tests assert constants (LH NEW-8). Re-derive them or drive `SimulationEngine`.
* The dormant `tests/test_sim/test_challenger_m2_it2_deep_stress.cpp` has not been built since an early commit and no longer compiles (retired slot-queue API): revive or remove (owner).
* Documents: the original's default scheduler is the **list scheduler**, not the 8 ms timing wheel (`[0x104b448]` = 0, `-newtask` selects the wheel): four places in `docs/reverse_engineering/movement/` and `GAME_REVERSE_ENGINEERING.md`
  still say wheel; AGENTS.md rule 8 still names a "Combat Ant guard post patrol" (the original only auto-engages); `tests/TEST_INFRA.md` and `TEST_READY.md` list the old melee model; `movement_tables.hpp` comment on sound flags.
* The differential harnesses written for the movement audit (tables, walk model, A* model) are worth keeping as regression tests (LM NEW-M8).

## 3b. Deliberate differences requested by the owner (tweaks)
The goal is a one-to-one copy; the owner asks for tweaks on top of it, each recorded here and in the CHANGELOG so that they are not mistaken for deviations:
* v0.0.51: the hit-point numbers above the ants (`Ctrl + L`) are **on by default** (the original starts with them off).

## 4. Decisions needed from the owner
* Start-up: the 3 s publisher-logo splash and its jingle are the original's artwork (LR R4.2a, LS NEW-7); the Single / Multi choice screen that the original shows (LR R4.2c); the "More Help" dialog and its dead web address (LR R4.2d, LU NEW-11).
* Web build: what "Leave" does on the results screen (the original exits the program).
* Text: anti-aliased bundled font versus the original's GDI draft quality (D4, BY DECISION so far).
* Developer shortcuts (team switch Ctrl+1..4 / Ctrl+C / Ctrl+Tab and others): keep for local games behind a developer switch, or remove (LI NEW-5).
* Input tick: queue mouse events to the original's 20 Hz input pass and read the pointer then (LI I-29), or keep event-time handling.
* Minimise: the original never pauses; the remake pauses the local simulation (LR NEW-7, LX).
* The minimap hit flash (see Batch 6): implement only if a screenshot of the real game confirms it.

## 5. Corrections to statements made earlier in this project
* "Hatch retry is about 8 ms" (audit H, docs) is wrong; the retry is 1000 ms (list scheduler).
* CHANGELOG (Unreleased, cleanup entry): "the death clips are no longer started by a separate effect" was wrong: only the unused helper was removed; the effect path in `movement_system.cpp` is still used.
* Audit R's row "ally_confirm = string 4" is the More Help dialog (`0x1016c09`); the real confirmation is `0x1016438` (built in v0.0.50).
* Audit B's golden timelines omit the doubled first frame of a clip started inside a callback (getpow is 840 ms, not 770); the remake already reproduces the doubling.
* Audit T's claim "0 seams at 13 window sizes" holds for integer scales only.

## 6. What static analysis cannot settle (needs the real game)
Screenshots of the original (cnc-ddraw in `Original-Ants/` saves the game's own 640 x 480 picture with the Print Screen key) would settle:
the minimap hit flash, the start view, the setup and results layouts and the start-up timeline, the splash jingle, the score-box edge and minimap frame, the real refresh cadence (hence bubble and task periods),
the 180 s life of fire walls and bridges (0.3-2.2 s spread), contact latency of fights, and whether pressed-button clicks play sounds at the press. A stopwatch recording of a fire wall's life would settle the timers.
