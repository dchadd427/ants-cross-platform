# Audit ledger: Status texts, voices, sound cues, music

Result of the residual-findings audit of this area against commit 4aa985f (v0.0.50 plus the cleanup pass). It compares the remake with `Original-Ants/Ants.exe` (Capstone disassembly) and `ants.chd`; nothing was run in the original. `<scratch>` and `SCRATCH` stand for a scratch folder of the audit session that is not part of the repository: the data files named below (probes, CSV tables, disassembly dumps) are not kept here. The synthesis and the ranked list of changes are in [`../AUDIT_ONE_TO_ONE.md`](../AUDIT_ONE_TO_ONE.md).

## Ledger

All paths below are under `SCRATCH/audit/LS/`, where `SCRATCH=<scratch>`. Per-row verdicts and the full sound audit are in these data files:
- `verdicts_sites.tsv`: the 50 rows of sites.tsv.
- `verdicts_cues.tsv`: the 57 cues.
- `verdicts_findings.tsv`: every S finding.
- `sound_audit_cue_sites.tsv`: the 21 `FUN_0102bd7e` sites plus the other primitives.
- `sound_audit_frame_sounds.tsv`: the 120 animation-sound families; `truncation.tsv` and `anims_with_sound.tsv` hold the underlying data.
- `sound_audit_engine.tsv`, `sound_audit_music.tsv`, `remake_audio_producers.tsv`, `positional_law.txt`.

Legend: CR = code reading, T = test binary run, P = probe. The probes (`probe/p_end.cpp`, `p_end2.cpp`) are linked against the frozen libraries; I never touched the repo.

| ID | finding | status | evidence | remains |
|---|---|---|---|---|
| S-0.1, 0.2, 0.5 | 500 ms flash in 50 ms steps, 5000 ms CLEARSTAT, one colour (79,0,143) | FIXED | `status_line.hpp:15-58`, `hud.cpp:323-328`; test_status_messages (256 checks, 0 failures); CR+T | - |
| S-0.3, 0.4 | string 5 is a status; 18-21 are quick-chat defaults | FIXED | `hud.cpp:124-125`, `game_strings.hpp:61-64` | title is "Ants" (BY DECISION D18) |
| S-1, S-3 (Q1-Q6), D1, D2, D3, D4, D19 | single slot, clear on deselect, quiet paths, no invented texts, flash table | FIXED | `hud.cpp:262-328`, `hud.cpp:1154`, `hud_input.cpp:339`; no invented literal left (grep); CR+T | O1 (139 px clip with the real font) UNVERIFIED |
| S-site-02..09 (texts 6-12, clear) | selection texts | FIXED | `hud.cpp:262-318`; selection/quiet-path/type-change tests; CR+T | text appears at the next HUD update (<=50 ms) |
| S-site-24, 36-41, D7, D8 | Stopping and move/attack/special acknowledgements | FIXED | FUN_0101b67b/b711/b78a and the caller tail (0x1028990-0x1028a0c) re-derived; `hud.cpp:229-253`, `hud_input.cpp:308`; CR+T | - |
| S-site-11..13 | hatch texts 14, 15, 16 (order 16, 14, 13; cost `min(score,200)`; 8 s) | FIXED | `sim_engine.cpp:599-628`; CR+T | in a network game the text arrives after the lock-step turn |
| S-site-10 | text 13 plus canthatch cue | PARTIAL | text FIXED; cue is positional (NEW-2) | NEW-2 |
| S-site-14..17, 21, 22, 25-32, 34, 35; D11, D13 | world-event texts 17, 48, 51, 52, 55-58, 60-62, 64, 65 | FIXED | producers at `movement_system.cpp:836,1438,1481`, `combat_system.cpp:136-147,249,532`, `action_system.cpp:182,291,346,402`, `ability_system.cpp:143,169,185,235`; 62 goes to the thief's owner; CR | rows 17/29: binary conditions of FUN_0101c4f2 (O4) not re-derived |
| S-site-23, D12 | thief at hill: text and flash FIXED | PARTIAL | `action_system.cpp:311-319` | anthill cue is positional (NEW-3) |
| S-site-33 | "Ready!" 63 at emergence | PARTIAL | FIXED except the claimed 1000 ms retry: the remake's comment calls `[task+0x1c]=1000` (0x1025100) a dead store (`action_system.cpp:19,241`) | UNVERIFIED which is right |
| S-site-18-20, S-4.T1,T2,T4,T5, D5 | CHECKGO thresholds, 200 ms poll, limit, digits | FIXED | `sim_engine.cpp:431-449`; P: cues 55/54/44 and texts 49/50/59 on the grid | - |
| S-4.T3, D6 | other end rules (nobody has eggs/hatch/ants; all remaining players allied and local holds top score; drop-out win test) | OPEN | original loops at 0x1024921-0x10249db; `handle_game_over` has one caller (`sim_engine.cpp:449`), `drop_player` has no win test | port both |
| S-site-42-49, D14-D17 | alliance, News Flash, dropout, chat rules | FIXED | `sim_engine.cpp:660-745,886`, `hud.cpp:1395-1435,1822-1870`; CR+T | chat wrap: NEW-10 |
| S-7, D9, D10 | voice map, auto-hatch | FIXED | original re-derived (even/odd `rand()` residues match `sim_engine.hpp:99-143`); `combat_system.cpp:556-563` | - |
| S-11.O6 | bomb path into "Ouch!" | NOT A DEVIATION | FUN_01010a03 has one caller (FUN_01020c70, reached only from the melee paths); bombs never reach it | - |
| S-11.O5 | clock master | FIXED by design | lock-step; P: game over one poll after 0:00 | - |
| S-11.O1-O4, O7 | clip, indent, `[map+0x70]`, FUN_0101c4f2, key codes | UNVERIFIED | no oracle | screenshots of the original |
| S-cue-00..56 | 57 cues | 51 FIXED, 2 PARTIAL (cue 9 slider voice; cue 40 invite queue, recorded in v0.0.50), 4 OPEN | cue 31/44 (NEW-1), cue 37 (NEW-3), cue 46 (NEW-2) | see `verdicts_cues.tsv` |
| C01-C21 (`FUN_0102bd7e` sites) | cue call sites | 16 FIXED, C04 PARTIAL, OPEN: C07, C08, C09, C14 | `sound_audit_cue_sites.tsv` | see NEW list |
| P01-P07 (other primitives) | the only DirectSound Play is FUN_0102e955, reached only via the clip stepper (FUN_0102b997) or the cue play; also clip start/AddChild, effect creators, re-attenuation, StopTracked, SetSoundVolume, MCI music | PARTIAL | `sound_audit_cue_sites.tsv` P01-P07 | tracked-stop and law missing (NEW-4, NEW-6) |
| S-10.X | tests to update | FIXED/moved | lines were rewritten in v0.0.38-0.0.50 | - |

**Asymmetric / two-machine cases** (complete list in `sound_audit_cue_sites.tsv`):

| cue | who hears it |
|---|---|
| allynot | proposer and decliner (fixed in v0.0.50) |
| allyyes | answering machine only |
| allypro | invitee only |
| allyon / allyoff | every machine |
| underattack | victim and its ally |
| anthill | victim only |
| exithill | hatcher only |
| scoreup / scoredn | every machine, every team |
| results sting | each machine, local-or-ally rule |

All of these match in target. I found no second case like allynot. The remaining asymmetry defects are not about "who" but about positional versus cue (NEW-2, NEW-3) and a double sting (NEW-1).

**Frame sounds.** 285 animations carry a sound. Flags are once=1 on 20 animations, dup=1 on all, track=1 on 1334 of 1344. The remake's clip table covers the 185 ant clips. The other 100 are cues, UI clips, effects and the splash, and each is mapped in `sound_audit_frame_sounds.tsv`. Animation 231 (`ATTACK`) is referenced by no bind table, so it is never played.

**Music.** Four files: intro plus a random one of Ants2a/Ants2b/AntsFun3. The in-game pick rule (never the same as last, first pick uniform) is FIXED. Gaps are in NEW-8; the full table is `sound_audit_music.tsv`.

## NEW deviations

| ID | finding (original evidence -> remake) | visible impact |
|---|---|---|
| NEW-1 | The match-end sting plays twice. The original plays one cue per machine (0x1015a4a). The remake emits targeted events in `sim_engine_impl.hpp:391-402` (P: snd 56 to winner, 42 to losers; allies both get 56) and `application.cpp:947-953` plays the scorecard's sting again. | every match end: +6 dB, may clip |
| NEW-2 | "canthatch" (antstop.wav) is a non-positional cue in the original (0x1010b97). The remake emits it with hill coordinates (`sim_engine.cpp:613`), so it is distance-culled. | hatch click with <200 points while scrolled away: faint or silent |
| NEW-3 | The thief alarm "anthill" is a non-positional cue (0x10218f2). The remake emits it at the raid tile (`action_system.cpp:317`), culled beyond 800 px. | the raided player hears no alarm when the view is away from the hill |
| NEW-4 | Tracked sounds are never stopped. FUN_0102c0db stops the old clip's still-playing buffers (flag bit5) on replace, and FUN_0102c245 / FUN_01008871 do so on removal (`[map+0x68]=1` at 0x100def3). The remake lets every sound play out. 109 instances outlast their clip. | bomb explosion cut at 680 of 1144 ms, fire-ant attack 120 of 366, set-fire 460 of 879, dive 420 of 993, grabs 80-300 of 429, UI clicks cut at button release |
| NEW-5 | Options Sound slider. The original applies the volume at release and plays the gantrdy test voice (0x101508a). The remake applies it while dragging, no voice (`hud.cpp:1276,1543`). | every change of the option |
| NEW-6 | Sound laws differ. Original: listener at the view centre, radius 2500, Chebyshev, attenuation `25*((SV*pct/100)-100)` hundredths of dB, pan applied to the far channel only. Remake: Euclidean, `1-d/800`, hard equal-power pan (`audio_mixer.cpp:292-309`). | at 800 px the original is -8 dB and the remake silent; at dx=221 the remake is hard-panned versus -2 dB far channel; SV 50 is -12.5 dB versus -6 dB |
| NEW-7 | Startup jingle: template 161 is added with AddChild, so the first step plays snd 6 (stereo, 2956 ms). Audit R inferred no jingle; the control flow says yes. The remake has no splash, and the mixer treats 2-channel PCM as mono. | once per start; vendor artwork, open decision |
| NEW-8 | Music. The original plays the intro once, then random in-game pieces. It closes the music on focus loss and starts a new random piece on regain. A volume change restarts the track, and the match end cuts it instantly. The remake loops the intro, ignores focus, applies volume live, and fades the match end over 1 s. | setup screen and alt-tab |
| NEW-9 | Invented sound: a bridge collapse with a swimmer plays splash.wav (`combat_system.cpp:433`). `dsplash` is silent in Table 4. | one 1.9 s sound per such collapse |
| NEW-10 | Chat body wraps at 21 characters (`hud.cpp:1816`); the original wraps greedily by pixels at 126 px. | every multi-line chat message |

Smaller items, in `sound_audit_engine.tsv`: sounds start on the 50 ms tick (E8); the mixer has 32 channels with priority, while the original is unlimited (E4); the `combat_system.cpp:525` comment "silent cue" is wrong, since sound 40 is audible; `application.cpp:1010` plays cantgo.wav on START with no second player and I found no original counterpart (UNVERIFIED).

## Coverage

**Ran:**
- `test_status_messages` (256 checks) and `test_app_integration` (188 cases, 6536 assertions), both 0 failures, copied binaries run from `run/`.
- Two sim probes: match end, and CHECKGO thresholds.
- A Python emulation of the original's dB/pan formulas (`positional_law.txt`).
- Disassembly of the cue play, stepper, DirectSound, scheduler, MCI and slider code; every one of the 21 call sites; all four voice functions; the wave table (durations computed).

**Read only:**
- The remake's status, world-event, alliance and chat code. I verified each of the 50 site rows against its producer and the tests, not by driving each site through a probe.
- The rows I did not probe one by one are the HUD/input, world-event and alliance/chat rows.

**Not verified:**
- Runtime behaviour: the splash jingle, the exact clip pixel, the chat indent, the conditions of FUN_0101c4f2, `[map+0x70]`, the hatch retry period.

Three helper agents for these rows died on the usage limit having produced nothing; every verdict above is mine.

## Top 10 to fix next

1. **End rules (S-4.T3/D6).** Port the CHECKGO elimination loop and the drop-out win test into `checkgo_poll`/`drop_player`. Tests that encode "runs to the clock": the game-over tests in test_app_integration, `test_challenger_m2_2`, e2e scenarios.
2. **Sound laws (NEW-6).** Implement the radius-2500 dB law and the pan in `AudioMixer`, with `gain = 10^(att/2000)` and `SV → 25*(SV-100)`. Add a golden table from `positional_law.txt`. The old audio_mixer spatial tests are the ones affected.
3. **Tracked stop (NEW-4).** Give each AudioEvent a source id (ant id + clip serial) and emit a stop on clip replace or removal; the mixer stops that source's channels. Golden cuts: bombex 680 ms, afat 120 ms. Doc comment `movement_tables.hpp:52-56` changes.
4. **Anthill alarm (NEW-3).** Emit it with world (0,0). Tests: `test_hill_actions:344` (only checks the event exists).
5. **Double sting (NEW-1).** Keep the sim events, because tests pin them (`test_app_integration:761-764,5341-5344`, `test_challenger_m2_2:461`), and drop the second play in `application.cpp:947-953`; or dedupe in ingest.
6. **Canthatch (NEW-2).** Emit with (0,0). `test_status_messages:589` keeps passing.
7. **Music (NEW-8).** Intro plays once then the next random piece; close on focus loss, new piece on gain; stop at match end; apply volume at release. No tests exist for music.
8. **Slider (NEW-5).** Apply at release and play GeneralReady. Options checks in `test_hud_layout` apply.
9. **Chat wrap (NEW-10).** Reuse `wrap_label_text` from v0.0.48; `test_chat_log_format` and `test_chat_rendering` change.
10. **Invented splash (NEW-9), then the NEW-7 decision.** Delete `combat_system.cpp:433`; any bridge-collapse test that counts audio events changes. NEW-7 (splash jingle) needs a decision on the vendor splash first.
