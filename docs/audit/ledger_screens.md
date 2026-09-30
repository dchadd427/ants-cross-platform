# Audit ledger: Results screen, options, setup screen, startup flow (stage R)

Result of the residual-findings audit of this area against commit 4aa985f (v0.0.50 plus the cleanup pass). It compares the remake with `Original-Ants/Ants.exe` (Capstone disassembly) and `ants.chd`; nothing was run in the original. `<scratch>` and `SCRATCH` stand for a scratch folder of the audit session that is not part of the repository: the data files named below (probes, CSV tables, disassembly dumps) are not kept here. The synthesis and the ranked list of changes are in [`../AUDIT_ONE_TO_ONE.md`](../AUDIT_ONE_TO_ONE.md).

Every finding of audit R has a verdict below. The stage R screens are still not built: no R finding is REGRESSED, 3 are FIXED, 3 are BY DECISION or UNVERIFIED, and the rest are OPEN or PARTIAL. I re-read the binary for each item I rely on, rendered the remake's screens offscreen, and ran two probes against the frozen libraries; nothing in the repo was touched.

Report R's claims hold in the binary. Only its text table row "ally_confirm" is wrong (R5e). The one thing the earlier audit missed that matters most is that match end plays the winner/loser sting twice (NEW-1).

Files are in `<scratch>/audit/LR/` (`$LR` below):
- `ledger_LR.csv`: the full 58-row ledger with evidence.
- `text_elements_status.csv`: current status of every screen text.
- `stage_R_outline.txt`: the ordered implementation outline.
- `img/*.png`, `render_screens.cpp`, `render_extra.cpp`, `probe/layout_probe.out`, `probe/sting_probe.cpp`, `d_*.txt` (disassembly dumps).

## Ledger
Remake refs are in `frozen_4aa985f/src/ants_app/`. "Orig" means I re-read it in Ants.exe.

| ID | Finding | Status | Evidence | What remains |
|---|---|---|---|---|
| R1.1 | Row text Y(0)=235, Y(i)=50i+273; portrait SetPos (60 \| 45,75; Y+20) | OPEN | Orig 0x10156a4-0x1015737. Remake `scorecard.cpp:124-168` has y=238, row pitch 35, x=54 | Use Y(i) |
| R1.2a | re_screen composite only | FIXED | `scorecard.cpp:108-117` | - |
| R1.2b | Name label (100,Y,385x50), cell 18, (239,231,223), max 35 | PARTIAL | Cell 18 done v0.0.48; x=90, white/grey (`:131,157`) | x, y, colour, cap |
| R1.2c | Numbers left-aligned at 485/534/555/576, w 49/19/21/20 | OPEN | Orig 0x10157e7.. Remake centres at 497/537/558/579 | Left labels |
| R1.2d | Portraits = animated AntSlot, remap {60,40,20,0}[slot] | PARTIAL | Orig 0x1021c1f: colours right. Static sprite at (54,227) | Animate, anchors, team pair |
| R1.2e | "Waiting for scores..." plus a 250 ms gate (msg 0x27, tick 0x10245ff) | OPEN | Orig ctor 0x10153a1. Remake shows rows at once (`application.cpp:947-958`) | Add phase, label |
| R1.2f | Leave button fires on release | FIXED | `scorecard.cpp:81-100`, test 6.3 | - |
| R1.3a-c | Rows are real players only; allies merge as "A & B"; sort is quitter last, score, tie local first | OPEN | Orig 0x1015136 re-derived. Remake separates the winner, sorts the rest by score, hides all-zero rows (`:33-64,147-152`) | Port `build_rows` |
| R1.3d | Net names, never colour words | PARTIAL | v0.0.46 names; "Green Team" fallbacks remain (`:11-16`) | Vanish with R1.3a |
| R1.3e | Sting rule: local==rows[0] or ally==rows[0] | PARTIAL | Ids 56/42 right; equivalent except the quitter | Quitter rule |
| R1.4a | Keys Enter C c Q q X x leave; Esc does nothing | OPEN | Orig 0x1015b17. Remake: Esc goes to setup (`application.cpp:793-798`), no shortcuts | Key map |
| R1.4b | Leave exits the process | PARTIAL | Native OK (`:224,500`); web and Esc go to setup | Owner decision (web) |
| R1.4c | Game-over triggers | PARTIAL | Clock FIXED (CHECKGO). Elimination and quit-Yes with one other side (orig 0x100c5b1, 0x101453f) not ported | Port both |
| R2.1a | Slider geometry and thumb position | FIXED | Orig 0x1011397/0x1011543. `hud.cpp:91-95` gives the same left for v=0..100 | - |
| R2.1b | value=((clamp(x,211,395)-211)*100)/185; moves on move only; callback once on release | OPEN | Probe: press at 300 gives 60% (orig unchanged), x=301 gives 61% vs orig 48% (`hud.cpp:1273-1292,1541`) | SliderModel |
| R2.1c | Hit rect x[188,419) y[y,y+20) | OPEN | Remake y 170..205 etc. (`hud.cpp:1274-1288`); probe | Exact rects |
| R2.1d | Sound dB curve plus `gantrdy` cue; music restarts a new random track; scroll W+0x5350=v | OPEN (scroll PARTIAL) | Orig 0x102d803, 0x100e714. Remake linear gain (`audio_mixer.cpp:514`), no cue, no restart | Implement |
| R2.1e | Persistence with the `min<=v<max` rule | OPEN | No file or localStorage in `src/ants_app` | ConfigStore |
| R2.2a/b | Chat / quick-help toggles | PARTIAL | Behaviour FIXED (`hud.cpp:508,1781`); change on press; quick-help flag inert without persistence | Release semantics, persist |
| R2.2c | Quick-chat Edit: 141x15 at (92\|302,370\|402), 12 px, (239,231,223), max 100, caret 150 ms | OPEN | Remake white at (93,371), limit 40, caret 750 ms (`hud.cpp:1077,1761`); probe | Port Edit |
| R2.2d | Enter closes; Esc, O, click outside do nothing | PARTIAL | Esc FIXED; probe: Enter while a field is focused stays open; click outside closes (`:1317`) | Enter always; drop click-outside |
| R2.2e, R2.3 | Sim not paused; Return fires on release | FIXED | `hud.cpp:1447-1452` | - |
| R3.1 | Map list sorted by byte strcmp, GAUNTLET first | PARTIAL | Orig 0x1013de7 and plain strcmp at 0x1034480. Header text and minutes identical (probe); order fixed TREASURE-first (`map_select.cpp:29-36`) | Alphabetical scan |
| R3.2 | Labels (36,312/380/447), (415,95+50i); all empty until a 500 ms refresh | PARTIAL | Heights and y done; x=38, white; no blank period | x, colour, 500 ms |
| R3.2b | Thumbs and drop button by ping | PARTIAL | Room rows FIXED (v0.0.46); local thumb immediate; Drop rect live locally (`:263`) | Drop only ping>1800 |
| R3.3a | START fires on release, needs ready, locks screen | OPEN | Orig button class 0x1011206/0x1011281; probe: START callback after PRESS | ScreenButton |
| R3.3b/c | Fog pair silent and default off; Leave exits | PARTIAL | Correct effect; both act on press (`:211,295`) | Release |
| R3.3d | Keys: Enter S s start; Q q X x leave; Up/Down; nothing else | OPEN | Orig 0x1014076. Probe: Left/Right/1-6/F/D act, Space starts, Esc quits, S/Q/X dead | Exact map |
| R3.4 | Invented click targets | OPEN | Probe: name and info boxes advance the map | Remove |
| R4.2a | 3 s splash: anim 161 logo on (7,11,15), unskippable | OPEN | Orig 0x100abc1/0x1017977; `img/x1_splash...` | Add |
| R4.2b | Loading at least 3 s unskippable, INTRO started once, first screen at or after 6 s | OPEN | Orig 0x10179ea/0x100af86. Remake: any key skips (`application.cpp:686`), 1.5 s (`:886`), INTRO loops from init (`:360`) | Timeline |
| R4.2c | Single/Multi screen (string 76) | OPEN, needs owner decision | Orig 0x1016dfc/0x1017055/0x1017082 | Decide |
| R4.2d | Quick help: START + More Help; keys Enter Esc C c X x; M opens dialog | OPEN | Remake Enter/Space/Esc (`:713`), no More Help; option not persisted | Keys, decision |
| R4.3 | INTRO once, then chained random tracks | OPEN | Orig 0x100e8cc chain | Chain |
| R4.4/4.5 | Exit and stage sounds | PARTIAL | See R1.4b and NEW-1 | - |
| R5a | Franklin Gothic Medium, no antialiasing | BY DECISION (D4) | Orig 0x102b05f; Libre Franklin bundled v0.0.49 | Antialiasing never separately asked |
| R5b | Cell heights 12/14/18/20/24/35 | FIXED | v0.0.48 | - |
| R5c | Origin top-left, wrap 0x102b0b5 | PARTIAL | Wrap and dialogs done; setup, results and options ad hoc | Convert the rest |
| R5d | Colours (239,231,223), (7,11,15), (31,23,51), (79,0,143) | PARTIAL | Dialogs and status exact; chat input (20,50,40) at (484,423) (`hud.cpp:507`) | Table test |
| R5e | R's row "ally_confirm = string 4" | WRONG | 0x1016c09 is the More Help dialog (string 95, (20,30) 290x100, 18 px); the real ally confirmation is 0x1016438 (built v0.0.50) | - |
| R8.1 | Edge-scroll pixel step | PARTIAL / UNVERIFIED | Implemented from decoded arithmetic (`edge_scroll.hpp`, 1153-row csv); no run of the original | Oracle |
| R8.2 | Splash jingle | UNVERIFIED | No cue-play call on the splash template; inferred silent | Oracle |
| R8.3 | Exact glyph shapes | BY DECISION (D4) | - | - |
| R8.4 | [W+0x3e] gate in Edit::OnChar | UNVERIFIED | Also gates the Ctrl-command switch (0x1026164); probably Ctrl held; no writer found | Low |
| R8.5 | Menu pointer | RESOLVED | Original draws its cursor after load (0x100ad11); remake draws it on all screens (see NEW-5) | - |
| R9 | Top screen takes all input; quit dialog keys; modal ignores keys; window close exits | FIXED | `hud.cpp:1601,1623`; `application.cpp:639` | - |

## NEW deviations
| ID | Finding | Status | Evidence | Who sees it |
|---|---|---|---|---|
| NEW-1 | The winner/loser sting plays twice at match end, and at the end tick instead of at least 250 ms later | OPEN | `probe/sting_probe.cpp`: sim queues sound 56 (target 0) and 42 (targets 1-3). `post_tick` plays those (`application.cpp:944`), then the modal plays its own (`:950`); `audio_mixer.cpp:200` never merges voices | Every player, every match |
| NEW-2 | Music fades over 1 s at match end; original closes the MIDI sequencer at once | OPEN | Orig 0x10226e4-0x1022724 (MCI "close AntsMidi"); `application.cpp:955` | Every match end |
| NEW-3 | F9 quick-chat field is focused when options opens | OPEN | Orig 0x1014c86 calls focus(1) on the first Edit; probe: remake active edit is -1 | Anyone typing right after opening options |
| NEW-4 | In-game quick help closes on a click anywhere; original only by its button (release) or keys; M opens More Help | OPEN | `hud.cpp:1254-1259` | F1 users |
| NEW-5 | Loading screen omits the clay speckle tiles and shows the pointer during loading | OPEN | `application.cpp:1233` excludes sprites 0 and 2; cursor drawn in every state (`:1183`); orig draws it only after load | Every launch (minor) |
| NEW-6 | Results hide all-zero rows and show sandbox teams | OPEN | `scorecard.cpp:147-152` | Local games |
| NEW-7 | Minimising pauses the game; original never pauses (closes MIDI on deactivate, restarts a random track on activate) | OPEN | `application.cpp:645-653`; orig 0x100e839 | Low |

## Coverage
Verified by reading the disassembly (all in `$LR/d_*.txt`):
- Results ctor, rows builder, show, tick, keys, leave and message handlers 0x10226da/0x10241f5/0x102422d.
- Options ctor, Slider, Edit and callbacks.
- Setup ctor, refresh, thumbs, list, keys and START.
- World startup, loader, bar, message 0, quick help, More Help, Single/Multi screen, start dialog, AntSlot, button class, music.

Verified by running code:
- Screens rendered through the real `Application` (dummy SDL driver): loading, quick help, setup, start dialog, HUD, options, quit, results.
- `layout_probe` (setup keys and mouse, option slider hit and value, results layout) and `sting_probe`.

Not verified, because no oracle exists here:
- Loading-bar steps: geometry and fill formula re-read, the 10..100 sequence taken from R.
- The jingle.
- Exact pixels of the edge-scroll step.
- Whether message 3 reaches other machines (inferred from the broadcast flags).

The owner's Windows copy (cnc-ddraw is in `Original-Ants/`) could settle those: screenshots of the splash, setup, results and options screens, and a note whether a jingle plays at start.

## Top 10 to fix next
This is the ordered stage R outline; the detailed version with VAs is in `stage_R_outline.txt`.

1. **S0 Foundations.**
   - ScreenButton = port of the button class 0x1010fcb/0x1011206/0x1011281: capture on press, callback on release inside, leaving cancels. `hud.cpp:1373-1452` already does this; factor it out.
   - Label clip and right-align-on-overflow, a real-time UiClock, and ConfigStore = 0x100c18f/0x100c20c/0x100c27f with the validity rule. Keys: Sound 100, Music 65, Scroll 50, Chat 1, QuickHelp 1, F9..F12 strings (max 100). Native file, web localStorage.
   - Tests: new unit tests only.
2. **Setup screen.**
   - Alphabetical list (GAUNTLET first) and the exact key map. Esc must not quit.
   - All actions on release, invented targets and Drop removed, labels at x=36 in (239,231,223), empty for 500 ms, START locks the screen.
   - Tests to rewrite: 8.2, 8.3, 8.5, the click-sound block (~5761-5790; keep sound at press), `test_network_app` 363-386 and 456-471 (send press and release), `test_text_sizes` x positions.
3. **Results rebuild.**
   - Port 0x1015136 (`ref_results_model.py` is right) and 0x10155ac.
   - Snapshot of real players only. WAITING phase of at least 250 ms with string 111, then rows with left-aligned numbers and animated AntSlot portraits.
   - Keys Enter C c Q q X x; Esc nothing; Leave = `quit()`.
   - Tests: 6.2 and 6.3 (`set_on_replay`); new golden scenarios for solo, merge, tie, quitter last, plus layout numbers and the 250 ms gate.
4. **Match-end audio (NEW-1, NEW-2).** One sting when the rows appear, using the original rule. Remove the sim-side winner/loser events from `handle_game_over` or skip them in the app. Stop music at once. Test 6.1 asserts those sim events and must change.
5. **Options sliders.**
   - SliderModel from `ref_options_model.py`: move-only, callback on release, exact hit rects, integer values.
   - Sound gain = 10^(((v-100)*25)/2000) plus the `gantrdy` cue; music restarts a new random track; scroll gets the integer.
   - Tests to change: 9.4 (its 280→320 drag expects above 0.6, the original gives 58) and `test_hud_layout` 792-830. New: mouse→value table {188:0,211:0,250:21,300:48,395:99,640:99}, dB table {1:-2475,50:-1250,65:-875}.
6. **Quick-chat Edit and persistence.** 100 characters, F9 focused at open, caret 150 ms, Enter always closes, no click-outside close, toggles on release, persist on every change. Test 12.59 (expects active edit -1 at open) changes.
7. **Startup flow.** Splash 3 s, loading of at least 3 s without input, full composite, no pointer, INTRO once then chained random tracks, quick help keys Enter Esc C c X x and M. Tests: 8.4 (INTRO loop) and the intro-flow block near line 7332. New: a simulated-clock test that no input is accepted before 6 s.
8. **Text and colour pass (NEW-4).** Table-driven test of `text_elements_status.csv`: chat input at (481,424) in (7,11,15), caret 150 ms, and the in-game quick help closing only by button or keys.
9. **Quit and game-over flow.** Port CountOtherSides 0x100c5b1 and the message 3 quitter parameter; CHECKGO elimination comes with the network item.
10. **Owner decisions before step 7:** Single/Multi screen (Multi has no lobby), More Help URL (dead), web Leave (reload vs setup), antialiased vs exact-pixel text, minimise-pause (NEW-7).
