# Audit ledger: Input: pointer, cursors, scrolling, pedestals, keys, view origin

Result of the residual-findings audit of this area against commit 4aa985f (v0.0.50 plus the cleanup pass). It compares the remake with `Original-Ants/Ants.exe` (Capstone disassembly) and `ants.chd`; nothing was run in the original. `<scratch>` and `SCRATCH` stand for a scratch folder of the audit session that is not part of the repository: the data files named below (probes, CSV tables, disassembly dumps) are not kept here. The synthesis and the ranked list of changes are in [`../AUDIT_ONE_TO_ONE.md`](../AUDIT_ONE_TO_ONE.md).

## Ledger

Full per-row evidence is in `SCRATCH/audit/LI/ledger_LI.csv` (46 rows; SCRATCH = `<scratch>`). I did not repeat the options, quick-help, setup-screen and button-on-release findings; they stay OPEN for stage R in `SCRATCH/audit/LR/ledger_LR.csv`.

| ID | finding | status | evidence | remains |
|---|---|---|---|---|
| I-01, I-02 | Ant boxes 40x48 / 58x62, half-open, 3x3 scan, last hit wins; no filters; fog only on the pointer tile | FIXED (v0.0.41) | `hud_input.cpp:72-90`; re-read 0x1026a39 and 0x1026904; `test_hit_boxes` passes | NEW-4 |
| I-03 | 20 Hz edge scroll, 5 px inner strips, position-dependent step, hot zones, gates | FIXED (v0.0.40) | `edge_scroll.hpp` equals 0x1026aa3 / 0x1027251 / 0x1027197 / 0x102fff8. ScrollToShow arguments re-read: margin 0, step 0, immediate. 1152 golden samples pass | catch-up after a slow frame not verified; NEW-9 |
| I-04 | 12 px bands, disjoint strips, CanScroll | FIXED | `probe_input`: all 17 old probe pixels now give the original's cursor, e.g. (12,240) Normal, (5,12) W | clamp still 441/439 (I-06) |
| I-05 | Minimap pixel formula, (221,220) square, right-click order | FIXED | `edge_scroll.hpp:92-116`; 0x1026640-0x10266d0; `hud_input.cpp:404-414` = 0x1027b98 | - |
| I-06 | View frame (16,21) 442x440 vs remake (17,22) 441x439 | **OPEN** | see below | see Top 10 |
| I-07 | Wheel / PgUp chat scroll; panning in dialogs | PARTIAL | panning in dialogs fixed (`input_tick` gate). Chat scroll kept under plan section 11 "kept", not an owner decision | owner to confirm |
| I-08, I-09, I-10, I-11, I-12 | Click at release point, band <=4 px; (255,0,0) 1 px band with 2x2 dot; positive-area pick, Shift rules; enemy hill selects base; latch is visual | FIXED | `hud.cpp:1193-1240,1425-1526`; `hud_input.cpp:187,327-359,384-397,482-501`; 0x1027530, 0x10277f4, 0x1028ee0 re-read | status text on Shift paths (NEW-2) |
| I-13, I-14 | Cursor table incl. 0x1026f91 special targets; the cursor while a dialog is open (the normal arrow, mode 1 set by the dialog's attach thunk 0x101279a; the INPUT decision does not run: v0.0.80) | FIXED | `hud_input.cpp:114-189` follows 0x1026aa3 step by step. 0x1026f91 re-read per type; matches `sim_engine.cpp:1096-1120`. 0x1026584 early return for dialogs. Cursor sprite follows GetCursorPos every loop (0x1030b7a), so per-frame draw is right | NEW-7 |
| I-15 to I-18 | Right button = left rules at release at the press point; pedestal rects and no re-press cancel; Stop rules; hatch / ally | FIXED | `hud_input.cpp:400-434,17,458-511`; `stop_ant` = 0x1028a60; rects = 0x1028d30 constants; `butcand` frame 0 carries sound 61 (CHD read) | NEW-10 |
| I-19 | Glow by cursor mode | FIXED (phase differs) | flags [5510]/[5514] set in 0x1027e65 | NEW-13 |
| I-20 | Ally attack confirmation | FIXED (v0.0.50) | `hud_input.cpp:241-260` | recorded: whole group waits |
| I-21, I-23 | Esc / F1 / Ctrl keys; chat box | FIXED | `hud.cpp:1599-1796` = 0x102609a. Edit class 0x1011d86: takes 0x20-0x7E and Backspace only without Ctrl and below 100 chars; a full or empty box does not consume the key. 0x10103eb / 0x10103ad (F9-F12 go to all) | - |
| I-22 | Invented hotkeys | PARTIAL | armed-order keys and Space are gone; Ctrl+N/P use ScrollToShow. Still present: NEW-5 | NEW-5 |
| I-24, I-25 | Modal keys; button class | PARTIAL | quit dialog FIXED (0x1014511); top-bar / All / Team fire on release and cancel on leave (`hud.cpp:1375-1444,1532-1539`). Options, quick help and setup: see the LR ledger | NEW-8 |
| I-26, I-27, I-28 | Gates for buttons; marker on every release; Ctrl+L digits | FIXED | 0x102737e gate order; marker flag set after refusals too (0x10278d5); 0x102d193 has no SetTextAlign, so top-left TextOutA (closes open item 4) | - |
| I-29 | 50 ms input tick | PARTIAL / owner decision | remake acts at event time with event coordinates (`application.cpp:863-883`). Original reads world+0x110 (polled each loop) when the 50 ms task pops the event; band end point is the last tick's | owner (open item 7) |
| C-0 / C-1 | Pipeline claims | CONFIRMED | 0x1031b63: 0x203 (double click) and everything above 0x205 ignored; style 3 at 0x10319b3; key ids at 0x1031bbd; only the top dialog receives input (0x1012b5b-0x1012c97) | - |

**I-06 evidence.**
- **Frame and mapping:** SetRect 0x1030198 is called at 0x100dcbf with `[world+0x4a5c]` = (16,21,458,461). The draw transform (0x102fb6d) and pointer mapping (0x102fcdb) both use frame minus client origin.
- **Shell and z-order:** The uishell is added after the map view. Children are prepended (0x10299ed) and drawn tail to head, so the shell is on top. The shell's black 1 px border at x=16/y=21 and its hole starting at (17,22) are confirmed by compositing it (`uishell_magenta.png`). The visible area is therefore the same, but every world pixel in the remake is 1 px right/down of the original.
- **Probe numbers:** `probe_input`: camera max (1479,1481) vs original (1478,1480); pointer (16,21) maps to world (699,699) vs (700,700).
- **Other places that depend on the origin:**
  - Renderer and HUD constants: `renderer.hpp:92-95`, `hud.hpp:44-45`.
  - Audio listener: +220/+219 at `application.cpp:924` vs original +221/+220 stored by 0x1030249 after every scroll; also `audio_mixer.cpp:299`.
  - Minimap marker: size trunc(442/scaleX)+1 (0x1009306).
  - Click marker, HP digits and Ctrl+N/P centring all follow the constants.

## NEW deviations

| ID | finding | status | evidence | visible impact |
|---|---|---|---|---|
| NEW-1 | Start view. Original scrolls the fresh view (origin 0,0) just enough to show the square (ax-160, ay-160)-(ax+192, ay+192) around the hill anchor tile (HUD constructor 0x100e458-0x100e4b1 into 0x1027197). The remake centres the 4x4 footprint (`application.cpp:198-206,421-428,1282`) | OPEN | `probe_start_view`: GAUNTLET green original (726,24) vs remake (772,69). Every mid-map hill is (+46,+45) px off (hill at view-local (250,248) vs (220,219)). Report I's "centres on the home tile" was imprecise | Medium: every match start and every team switch |
| NEW-2 | Shift+click add, toggle off and Shift+drag add post the panel text (SetPanelMode d=0, e.g. 0x1027947, 0x1027aae). The remake marks them quiet and keeps the old text (`hud_input.cpp:332-340`, `hud.cpp:1211-1219`) | OPEN | `probe_shift_status`: worker + bomber, toggle off the worker gives "Ready!" (original "BomberAnt selected."); toggle off the last ant keeps "Ready!" (original clears) | Low |
| NEW-3 | BTNPUSH presses the pedestal through the normal chain. Frame 0 of `butXXX2d` carries sound 89 (CHD read; `butcand` 61). The original clicks after every accepted order with an unlatched pedestal; the remake is silent (`flash_pedestal`, `hud_input.cpp:195-199`; 89 only at real presses) | OPEN | 0x10285a4 into 0x1028360 mode 2 | Audible on every order |
| NEW-4 | Several ants on one tile: the original takes one representative (0x100f4ab, then 0x100f2cd: first found over teams 0-3, replaced by later unfrozen, stationary, preferably local ants). The remake tests all and the last in vector order wins | OPEN | `hud_input.cpp:76-88` | Low: stacked ants only |
| NEW-5 | Developer keys that do nothing in the original: Ctrl+M, Ctrl+T/F3, Ctrl+C, Ctrl+1..4, Ctrl+Tab, Shift/Ctrl+F12, Alt+Enter, Cmd+F. They run before the dialog gate; Ctrl+1..4/C/Tab call `HUD::init` (wipes chat log, closes dialogs, clears selection) | OPEN (kept by plan section 11, not an owner decision) | `application.cpp:793-847`, `hud.cpp:103-172` | Medium if pressed by accident (Ctrl+C) |
| NEW-6 | The original's dialogs stack (0x1012b5b pushes unless an exclusive one is open; the top takes all input). The remake allows one at a time and holds alliance offers until the other dialog closes | OPEN | `hud.cpp` `update_alliance_dialog` guard | Low |
| NEW-7 | Thief target cursor over a hill of any other colour, also colours without a player (0x102701e uses only FUN_0100ecac); the remake removes absent teams' hills | OPEN | `sim_engine.cpp:1096-1120` | Low: networked games with fewer than 4 players |
| NEW-8 | Quit and alliance dialog buttons keep the pressed state after leaving and re-entering; the original's OnMove cancels for good | OPEN | `hud.cpp:1461-1488,1554-1564` | Low |
| NEW-9 | The original is DirectDraw full-screen exclusive (SetCooperativeLevel 0x53 at 0x102c8eb, SetDisplayMode 640x480x8 at 0x102c96c), pointer confined and polled every loop. The remake is windowed without grab, scroll is gated by `mouse_has_moved_`, and the software cursor sits at (320,240) until the first motion (`application.cpp:462-465,786`) | OPEN (code reading only) | no ClipCursor import; no `SDL_SetWindowGrab` / `SDL_GetMouseState` in the source | Medium: edge scrolling feel; cursor jumps to centre at every match start |
| NEW-10 | Original shares one press point and one capture for both buttons; a right press during a left drag moves the band anchor | OPEN | 0x102737e vs `hud.cpp:1365-1421` | Rare |
| NEW-11 | Minimap view marker colour (251,251,255) (0x1009b6f) and size trunc(442/sx)+1 x trunc(440/sy)+1 clipped to the minimap; remake is white with its own frustum formula (`hud.cpp:706-727`) | OPEN | - | Low |
| NEW-12 | Feedback (pedestal pop/flash) in the original follows "any ant needed an order" (return of 0x10287b5); voice only when the closest ant's order was accepted. The remake uses the closest ant accepted for both (`sim_engine.cpp:1038-1090`) | UNVERIFIED | depends on when `issue_order` refuses | Low |
| NEW-13 | Glow phase is wall-clock (`SDL_GetTicks()%510`); the original restarts it on a mode change | OPEN | `hud.cpp:1103-1126` | Low |

## Coverage

- **Run code:** I rebuilt `test_pointer_model` (329 checks), `test_input_model` (70) and `test_hud_layout` (647) from the frozen copy; all pass. Probes I ran: `probe_input` (edge bands, origin constants, camera max), `probe_start_view` (start view per map and team), `probe_shift_status`, `probe_hills`, and a Python composite of the uishell for the hole and border.
- **Re-read in Capstone:**
  - Input and order path: 0x102737e, 0x102653f, 0x1026aa3, 0x1026904, 0x1027530, 0x10277f4, 0x1027b51, 0x102609a, 0x1028ee0, 0x1028a60, 0x1028c44, 0x1027f07, 0x10285a4, 0x10287b5.
  - View and scroll: view class, ScrollToShow, list functions, edge and minimap scroll code, start view.
  - Dialogs and controls: dialog manager, edit class, key and mouse translation.
- **Only by reading:** timing model, glow, NEW-4, NEW-6 to NEW-13.
- **Not verifiable without a runtime oracle:** hit-rect union (report I open item 2), sound-at-press (item 5), scheduler catch-up, how the pointer behaves in the remake's window on macOS. Screenshots or video of the real game would settle these.

## Top 10 to fix next

1. **I-06 view origin.** Set `PLAYFIELD_X/Y` to 16/21 and camera viewport to 442x440; keep the clip at (17,22) 441x439 under the shell. Also fix listener centre (+221/+220), minimap marker size, and clamp. Tests to rewrite: `test_app_integration.cpp:316-339`, `test_hud_layout.cpp:718-719`, and the `test_render_parity` reference model.
2. **NEW-1 start view.** Use `detail::scroll_to_show` from origin (0,0) with the rect around the anchor, including after `set_local_player`. Add a golden per map and team. No existing test pins the start centring.
3. **NEW-3 order click.** Play `NavButtonClick` in `order_feedback` when a pedestal is flashed (not when a latched one pops). Change `test_pointer_model.cpp:442`, which assumes the voice is the last sound.
4. **NEW-5 developer keys.** Move them behind a developer flag or at least behind the dialog gate; remove Ctrl+C and Ctrl+Tab team switching from normal play.
5. **NEW-9 pointer.** Grab the pointer while playing and read `SDL_GetMouseState` each input tick; drop the (320,240) reset and the `mouse_has_moved_` gate.
6. **NEW-2 Shift status.** Drop `selection_status_quiet_` on Shift add, toggle and drag add; extend `test_status_messages`.
7. **NEW-4 tile occupant.** Port 0x100f2cd's choice into `pick_ant_at`; add a two-ants-on-one-tile test.
8. **I-29 input tick.** With owner approval, queue events to the 20 Hz input tick and read the pointer at processing time; keep the band's end point at the last tick. The pointer-model tests call `handle_mouse_*` directly and would need a drain step.
9. **NEW-6 / NEW-8.** Dialog stack for alliance offers; cancel pressed state on leave for dialog buttons.
10. **NEW-11 / NEW-13 / NEW-12.** Marker colour and size, glow phase, and the feedback/voice conditions (check refusal cases first).

Files are in `SCRATCH/audit/LI/`: `ledger_LI.csv` (full table), probes (`probe_input.cpp`, `probe_start_view.cpp`, `probe_shift_status.cpp`, `probe_hills.cpp`), `build.sh`, `dfind.py`, `uishell_magenta.png`, and disassembly dumps (`d_*.txt`).
