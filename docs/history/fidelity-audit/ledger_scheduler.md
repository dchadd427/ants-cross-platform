# Audit ledger: The original's scheduler tasks and timed behaviour

Result of the residual-findings audit of this area against commit 4aa985f (v0.0.50 plus the cleanup pass). It compares the remake with `Original-Ants/Ants.exe` (Capstone disassembly) and `ants.chd`; nothing was run in the original. `<scratch>` and `SCRATCH` stand for a scratch folder of the audit session that is not part of the repository: the data files named below (probes, CSV tables, disassembly dumps) are not kept here. The synthesis and the ranked list of changes are in [`../AUDIT_ONE_TO_ONE.md`](../../AUDIT_ONE_TO_ONE.md).

## LX: census of the scheduler tasks of Ants.exe

The full table (43 classes, 16 non-task timing behaviours, ranked deviations) is in `<scratch>/audit/LX/census_table.md`. All VAs are in Ants.exe. "Remake" means frozen_4aa985f.

### 0. Corrections to the brief

1. **The default scheduler is the LIST scheduler, not the 8 ms timing wheel.**
   - The engine constructor at 0x103155c does `cmp [0x104b448],0; je 0x1031583`.
   - 0x104b448 is in the .bss tail of .data, so it starts at 0. Its only writers are the command-line parser's `-oldtask` (0x103185c) and `-newtask` (0x1031875).
   - 0 builds `FUN_01030d6a`, the list scheduler (vtable 0x1005208). 1 builds `FUN_01031211`, the wheel.
   - The wheel and its 8 ms slots exist only with `-newtask`.
2. **How the list scheduler works.**
   - Add is 0x1030e7b. A task is inserted with due = `timeGetTime()+[task+0x1c]`, sorted by due time, ties after existing entries.
   - RunOne (0x10310e8) runs ONE due task per call. After a body returns 1 it re-arms at `timeGetTime()+[task+0x1c]`, so the real period is interval + run time + latency.
   - The body is vtable slot 3 (+0xc): `call [eax+0xc]` at 0x1031156 (list), 0x1031413 (wheel) and 0x1030fec (thread). It returns 1 to re-arm and 0 when finished.
3. **The main loop (0x1031916).**
   - It calls PeekMessage, and only when the queue is empty calls RunOne. There is no Sleep, so it busy-spins, and window messages always run before tasks.
   - The exe imports only timeGetTime, Sleep and WaitFor*. There is no SetTimer, GetTickCount or timeBeginPeriod.
4. **`[task+0x1c]` is live.** Several bodies write or read it as the interval, which only works in list mode:
   - HATCHTSK writes 1000 at 0x1025100.
   - KWFO writes 200 at 0x10254b0.
   - REMOTE and PLAYBACK write it.
   - TXTFLASH reads it as its decrement at 0x102b6a5.
   - Under the wheel that field is the slot number, so list mode is the designed mode. The remake's comment "[task+0x1c]=1000 is a dead store" is wrong.
5. **Effective periods.**
   - REFRESH has interval 0 and its body waits for the DirectDraw flip (FUN_0102ccdc, FUN_0102cd36).
   - On a vsynced flip chain every task is therefore serviced on a grid of display refreshes (a 50 ms task runs every 50 to 66.7 ms at 60 Hz).
   - timeGetTime ticks at the OS timer tick.
   - UNVERIFIED on a real cnc-ddraw setup. A runtime measurement is needed.

Four task classes outside the brief's 31 names exist: REMOTE, CUSS, KWFO and Invuln. There are 43 classes in total.

### 1. Task table

Add is (delay, interval) in ms; T marks a task with its own thread. Bodies are slot 3.

| task | Add / body | what it does | remake | verdict |
|---|---|---|---|---|
| INPUT | (0,50) / 0x10242c5→0x102603f | drains the key and button queue, then the hover and edge-scroll step | `handle_camera_panning`, `input_tick` | MATCH for the 50 ms cadence; clicks are handled at once, not at the next pass |
| CUSS | (0,1000) / 0x10252b5 | sweeps the list of one-frame `empty.bmp` sound-carrier effects (anims 213, 216, 220) | AudioEvent plays directly | MATCH, no visible effect |
| REMOTE | (0,50) / 0x10242d0 | drains the network message queue | lock-step runner | not applicable |
| CLEARSTAT | two-phase (0,5000) / end 0x1024c9c | clears the status line 5 s after the last post | `StatusLine` kLifeTicks=100 | MATCH |
| ARQTASK | (0,1000) / 0x1024808→0x100fe93 | pops one queued team invitation per pass and opens the dialog (strings 1 and 2) | `propose_alliance`, `update_alliance_dialog` | DEVIATION, minor |
| ADDPLYRT | (0,0) / 0x1025348 | lobby host re-sends MAP_SELECT | room | not applicable |
| ANTHILLQ | (0,200) / 0x10247f9→0x100ff1f | hill queue dispatch | `anthillq_run`, period 200 | MATCH |
| HATCHTSK | (8000,0) / 0x1025072 | if an own ant stands on the entrance it retries in 1000 ms | `kHatchRetryMs = 8` | DEVIATION |
| CURSORTASK | (0,150) / 0x1011e6c→0x1011ccf | toggles the text-edit caret | hud.cpp:504, 1083 | DEVIATION |
| CHATSCRL | (0,100) / 0x1025234 | chat scroll-bar auto-repeat, 15 px per pass | none | MISSING |
| CHATAPPD | (0,50) / 0x1025282 | smooth follow of new chat lines, 5 px per pass | none | MISSING |
| KWFO | (5000,200) / 0x10254b0 | keeps the match-start modal until released and at least 5 s old | fixed 5 s | MATCH in single player; in net play the modal closes at 5 s even if the barrier is open |
| NETINIT | (0,500) / 0x10243dd | host builds and sends ROSTER | net | not applicable |
| STOPTASK | (0,5000) / 0x102441e | re-broadcasts STOP for idle own ants; the local handler skips own ants, remote copies re-snap | not needed | not applicable, no local effect |
| FDTASK | (0,3000) / 0x1025063→0x100fc0d | flower droppers, see below | per-tick exact interval | DEVIATION, small |
| UPDSDLIST A / B | (500,0) / 0x102454d and (250,0) / 0x10245ff | lobby refresh; results screen waits for all SCORES | room, scorecard | not applicable |
| PATHMGR | (0,50) / 0x1024786 | one A* slice per pass | 50 ms per player | MATCH |
| CHECKGO | (0,200) / 0x1024839 | clock warnings and end-of-match rules | `checkgo_poll` | PARTIAL, see below |
| CHECKDROP | (0,500) / 0x1024ad5 | drains dropped players | `drop_player` | timing not applicable; the end-of-match branch is missing |
| Invuln | two-phase (0,0) | flag lives one pass | none | MATCH |
| COMBEVT | delay from caller / end 0x1024c69 | combat-ant auto-engage after 2000 or 3000 ms | `start_combat_timer` | MATCH |
| BTNPUSH | (0,125) | pedestal button pressed 125 ms | `btnpush_until_ms_` | MATCH |
| ANTPAUSE | (0,300) | blocked-step wait | kPauseMs=300 | MATCH |
| TIMEOUT | (0,2500) / 0x1024dae | fire wall or bridge expiry at 180 s of match time, polled every 2.5 s | 3600 ticks | MATCH, nominal |
| PLAYBACK | (1000,0) / 0x1024fcb | observer replay with `-I<file>`; Space toggles it | none | MISSING (feature) |
| SCRBUBBLE | (0,20) / 0x1025479 | 20 passes of 5 px | 20 x 20 ms | MATCH |
| CLEARSEL | two-phase (0,250) | 250 ms click lock after Stop | `input_lock_ticks_=5` | MATCH |
| TXTFLASH | (50,50) / 0x102b6a1 | status text flash | kFlashTicks=10 | MATCH |
| REFRESH | (0,0) / 0x102c278 | update, draw and flip on every pass | 60 fps loop | MATCH in kind, rate UNVERIFIED |
| SOUNDS/500 | (0,500) / 0x102f4f1→0x102f777 | re-pans and re-volumes playing positional sounds from the view centre | `set_listener_position` every frame | DEVIATION |
| SOUNDS/5000 | (0,5000) / 0x102f4dd | frees finished sound buffers | n/a | MATCH |
| VIEWPORT | (0,0) / 0x102d88d | smooth scroll | instant | MATCH, dead code |
| THROWEXC | (0,0) / 0x1030b55 | error box from a net callback | none | not applicable |
| MOUSE | (0,50) / 0x1030b7a | samples the pointer and moves the cursor sprite | per-frame cursor | DEVIATION (feel) |
| JOYSTICK | (0,20) / 0x1030b69 | polls the joystick | none | MISSING (feature) |
| BPDPSC, SCKA, SINC, SOUT | threaded, at 0x1033364, 0x10335c4, 0x103367c, 0x103378e | connect, accept, recv, send | net | not applicable |
| BPDPPULSE, BPDPPERF | (0,[NetMgr+8]) / 0x1033874 and (0,1000) / 0x10339ca | pulse and lobby ping | net | not applicable |
| CLRSOCK | (0,0,T) / 0x1033b34 | closesocket | net | not applicable |

Details on the rows that are not obvious:

- **VIEWPORT.**
  - Its only creator is FUN_0102fff8 (ScrollToShow), and only when its instant flag is 0.
  - The sole caller, FUN_01027197 at 0x10271bc, does `push 1` (0x10271b9), and no pointer to 0x102fff8 exists anywhere.
  - So the task is never created; the remake's instant scrolling is correct.
- **CHECKGO.**
  - Warnings: clock below 0xee48 gives cue and text 0x31, below 0x7918 gives 0x32, below 0x2af8 gives 0x3b every second.
  - The end rules are: (R1) clock below 0; (R2) no non-dropped team has any ant, egg or running hatch; (R3) more than one team exists and every survivor is allied, in which case the machine of the best combined-score team sends GAMEOVER.
  - FUN_0100d03b also sends GAMEOVER when no other non-allied team is left.
  - The remake has R1 and the warnings; its source comment says the elimination rules were left out.
- **FDTASK.**
  - A dropper fires when `param*1000 < now-stamp`, checked only every 3 s.
  - Level params are 8 s (MEDIUM centre) and 20 s (TREASURE), which become 9 s and 21 s.
  - 15, 30 and 60 s sit on a multiple of 3 s, so they fire at that multiple or one pass later because of the strict `<`.
  - A blocked drop waits for the next pass (up to 3 s); the remake re-checks every 50 ms.
- **ARQTASK.**
  - Invitations are queued only when the message's target is the local player (handler 0x1023a80).
  - The dialog and the allypro cue come up to 1000 ms later, one per pass, in FIFO order.
  - The remake opens the dialog within one 50 ms tick and plays the cue at proposal time, even if a modal is open. It keeps one pending slot per invitee.
- **CURSORTASK.** The caret toggles every ~150 ms. The remake uses 750 ms on and 750 ms off.
- **SOUNDS/500.**
  - The original uses Chebyshev distance with a dB-linear volume and a ±25 dB pan (FUN_0102e8e4, FUN_0102d803).
  - The remake uses Euclidean distance with linear amplitude and equal-power panning.

### 2. Timed behaviour that is not a named task

- **Start-up:** a 3 s publisher-logo splash and a ≥3 s loading screen, each a `while(timeGetTime()<deadline) Sleep(100)` loop, unskippable. The remake has no splash, a 1.5 s loading screen, and it is skippable. This is DEVIATION and already known.
- **Match clock (FUN_0100fa50):** synchronised to a clock master. The remake's tick clock is MATCH by design.
- **MATCH:**
  - alarm cue cooldown 10000 ms (0x1010a08, `kAlarmCueGapMs`);
  - combat auto-engage 2000 ms (0x101c0f1);
  - battle cloud 3000 and 1000 ms (0x101a329);
  - hill arrival stamp (0x101cd52);
  - real-time animation clock with catch-up (0x102b95f);
  - start voice chosen by `timeGetTime()%6` (0x100e4bd), which the remake does with `rand()%6`.
- **Minimap hit-flash, MISSING (new).**
  - StartEngaged stamps ant+0x9c at 0x1020cd1.
  - The ant's minimap colour function (vtable+0x44 = 0x101a88c, called by the minimap draw at 0x1009a69) then alternates palette index 250 and the team colour every 200 ms for 5000 ms.
  - This applies to the local team and its partner only, so every fight flashes on the minimap.
  - The remake's `render_radar` uses static colours.
- **Pointer sampling, DEVIATION (feel).**
  - `GetCursorPos` is called only in the MOUSE task (0x1030a8c). WM_MOUSEMOVE is ignored, and button messages carry no coordinates.
  - The cursor sprite and every click therefore use a pointer sample up to 50 ms old.
- **Key repeat limit:** the input layer drops same-channel events closer than 50 ms (0x1030860). The effect is negligible.
- **Minimise:** the original has no pause. A lost surface only skips drawing and removes MOUSE and JOYSTICK, and all other tasks keep running. The remake pauses the local simulation on minimise (application.cpp:645-649), which is INVENTED.
- **Debug and feature items:**
  - `-sleep:N` is parsed but never read;
  - `-Count` draws an fps overlay;
  - `-O` and `-I` are recording and playback, which the remake lacks.

### 3. Ranked deviations by visible effect

1. **Match end rules R2, R3 and the drop handler's end are missing.**
   - A multiplayer game decided early, or a solo game with no ants and no eggs, does not end until the clock runs out.
   - Fix: port the rules from the CHECKGO row through the lock-step command path.
   - Tests that assume the clock is the only end: test_sim_rules.cpp, test_commands.cpp, test_challenger_m2_*.cpp.
2. **Minimap attack flash is missing.** Add a hit stamp in `start_engaged` and toggle every 200 ms for 5 s, using palette 250 in `render_radar`.
3. **Chat has no scroll bar, 15 px auto-repeat or 5 px follow animation.** It needs a pixel-based view. Wheel and PageUp tests in test_app_integration.cpp (about lines 2563-2571) encode the old line scroll.
4. **Chat caret blink is five times too slow.** Change `/15` to 3 ticks at hud.cpp:504 and 1083.
5. **Hatch retry is 8 ms instead of 1000 ms.** Set `kHatchRetryMs = 1000`. Test "2.4" waits up to 200 ticks, so it still passes.
6. **Pointer and clicks are taken per frame instead of from 50 ms samples.** Take the pointer only in the input pass.
7. **Start-up splash and loading screen** (stage R).
8. **Flower dropper quantisation:** 8 s becomes 9 s and 20 s becomes 21 s; blocked drops retry per 3 s pass.
9. **Invitation dialog and cue timing and FIFO queue.**
10. **Positional sound:** 500 ms update and the different law.
11. **Minimise pauses the game.**
12. **Net start modal closes at 5 s** even if the barrier is open.

Not deviations: VIEWPORT, CUSS, SOUNDS/5000, Invuln and STOPTASK. The net tasks are replaced by the remake's own networking.

### 4. Coverage

- **Verified in the disassembly or by probe:**
  - the scheduler selection, Add, RunOne and the main loop;
  - every Add call site, decoded with `addsites.py` and then re-read;
  - the bodies and helpers of all tasks except the net ones;
  - the level-file dropper parameters, parsed with `audit/LE/lvl.py`;
  - the CHD animation ids used to identify the CUSS effects.
- **Net task bodies and schedules** are taken from audit N, with the Add arguments re-checked.
- **Remake side** was read and grepped only. I built and ran nothing.
- **Not verified:**
  - real task periods under a DirectDraw or cnc-ddraw setup;
  - the exact range value behind the positional-sound formula;
  - the allypro cue slot (game+0x4900), which comes from audit N.
- **Helper agents:** two background agents died on the rate limit without writing anything, so none of their output is used.
