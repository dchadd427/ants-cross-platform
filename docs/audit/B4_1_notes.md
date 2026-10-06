# Bots B4-1: the standard bot (what was built, measured, left out, and where it differs from the design)

## State at handoff (2026-10-03, after the independent review; the work moves to a session that has no community maps and no original program)

The review played the standard bot on a library of community maps (587 files: 47 do not load, 6 are unplayable, 1 has fewer than two hills, 533 were played; alone and four Hard bots, two seeds each). It found **no fairness breach** (view, budget, determinism, controller filters, server parsing all verified) and the economy findings below. The review's drivers and result files are not in the repository (the maps never are); `bot_arena --maps-dir DIR` plays any library read-only.

| Finding | State | Exact next step |
|---|---|---|
| **H1** Hard turned off the economy's rescue and the carrier aid whenever the gate was in the plan, but the gate does nothing on a hill with fewer than two walkable queue tiles or no buffer tile (searched 2 to 5 rows north of the hill: a hill in the top rows has none): carriers stood idle with their food (one seat banked 546 points on average where the worker banks 4,000) | **Done**: `GateTask::usable()`; the rescue and `CarrierAidTask` stay on while it is false; test AI10.4 (fails without the fix) | Optional: search the other directions for the buffer tile so that such hills are guided too |
| **M1** a refused intent has no Fate (`bot_controller.cpp`, where the filter refuses it), so a task proposes the same refused click again and again (a pile whose click tile holds a power-up: one seat banked 1,150 to 2,150 points where 6,000 are possible) | **Done**: `Bot::Fate::Filtered` (the controller tells the bot at the look); `HarvestTask` blacklists the pile, `RaidTask` the team, `GateTask` the tile, the economy's rescue and `CarrierAidTask` the entrance, each for 900 ticks; tests AI2.21, AI7.27, AI7.28, AI10.5, AI13.4 (each fails without its code) | None. Refused clicks on the community maps (four Hard bots, two seeds, 4,080 seats): 7 before, 78 after, all on one map whose two dead hills refuse every click onto the entrance (a seat that was refused once used to freeze; it now tries again once per 900 ticks); no other seat is refused anywhere |
| **M2** a seat that spends its whole command budget all match and banks nothing (25 ants a team, seed 2, seat 0: 4,086 commands, score 0: about 1,290 re-orders onto the gate's buffer tile and about 480 raid orders at one entrance); the same on a few other maps | **Done**: the cause was a causeway jammed head on, so that the carriers could not get in: the gate clicked the entrance about every 20 ticks for the rest of the match with the rescue and the aid off. `GateTask` stops for 900 ticks after 16 clicks in a row that delivered nothing, and `StandardBot` has a stall detector (`docs/BOTS.md`, "The stall detector"); tests AI7.29, AI10.6, AI10.7 (33 mutants, all caught) | None. Community maps (four Hard bots, two seeds): seats that sent a command a second or more and banked nothing 30 → 1, commands -17 percent, banked +0.15 percent; 586 of 4,080 seats stall at least once. Shipped maps: Easy and Medium identical, Hard differs in 7 of 48 matches (score total +0.04 percent) |
| **L1** S3.71's bound `ms_per_second < 100` failed once on a loaded machine | **Done**: S3.71 times the CPU time of the server's thread (`CLOCK_THREAD_CPUTIME_ID`, `GetThreadTimes`) and checks its clock first | None |
| **L2** `PowerUpTask::try_start` evaluates `power_up_side` inside the `stable_sort` comparator (a map with about 290 Fire power-ups cost 7 s of bot code) | **Done**: `power_up_side` is asked once per power-up; no result changes (72 shipped-map and 140 community-map matches end on the same hashes) | None |
| **L3** documents | **Done**: `docs/NETWORK_PORT.md` (the room specification, the status JSON), `docs/BOTS.md` and this file | None |
| **L4** the status JSON gives `style` "random" for the worker and idle kinds, which have none | **Done**: the status JSON has no `style` for the worker and the idle bot; S3.65 checks it | None |

**Community maps, in general terms** (the 227 maps on which the worker alone scores at least 400, two seeds summed): the standard bot banks 1.149 times the worker's score with the tree of this file before H1 and 1.169 times with H1; the maps on which it banks under 75 percent of the worker: 15 before, 7 with H1; 7 maps went from under 75 to at least 90 percent. After M1 and M2, the same method on the library of the session that fixed them (225 maps on which the worker alone scores at least 400, where the review had 227): **1.165 before and after** (the standard total 2,316,891 before, 2,316,806 after), **7 maps under 75 percent, the same seven with the same scores**: M1 and M2 do not reach them (alone, no stall, no refused click). Three copies of one map are noise: the worker's own score swings between 0 and 1,890 over seeds 1 to 4, and over four seeds the Hard bot scores 3,210 against the worker's 2,580. On a map of five ants, four of them on an island that the hill cannot be reached from, the Hard raider's opening trip for the Fire power-up takes the only ant that can deliver over to that island, where the harvest never sends it again: 0 points where the worker banks 330 (`secure=0` gives 330 back). On a third map the Hard bot banks 75 percent of the worker over six seeds (the raider style 101 percent, the aggressive style 84). On two copies of a map with a pot of 1,280 it banks 4,830 against 7,680 over six seeds at every style, and `walls=0` or `steals=0` alone brings the worker's score back: the trips of the Fire power-up and of the stolen Thief take scarce ants. These are choices of the opening, not defects of M1 or M2; they are left to the owner. On the shipped maps H1 changes nothing: the review's 36 matches there end on identical hashes with it; M1 changes nothing there either, and M2 changes nothing at Easy and Medium (96 matches, seeds 1 to 8, identical hashes); at Hard 7 of 48 matches differ (11 stalls in all), the score total 182,035 before, 182,115 after. The tables A1, A2, A4 and the ladder below were measured before H1 and still stand.

**Separate task, not the bots:** the review reports that the original has a stun-time invulnerability (the Invuln task, started from `FUN_0102151a`, action 3) that the engine lacks. It is logged as an engine fidelity task and is not verified against the program here.

Update for the next section of `implementation_plan.md` (that file is not part of the repository; the owner copies this text into it). Milestone B4-1 of the computer players, as designed in [`docs/BOTS.md`](../BOTS.md): the standard bot of the three levels and four styles, on top of the task model and the worker bot of B3. Status: built, every suite of the quick tier passes, the mutation check below is done, the release is v0.3.0 (`VERSION`, `CHANGELOG.md`, `STATUS.md` and the version line of the README are the coordinator's to move: a draft entry is at the end of this file). The numbers are those of the tree this file ships with: the arena of the final tournaments was built from the tree before the last two commits, which rename two loop variables (a GCC warning) and add a test of random worlds; nothing in the bots changed. **What the standard bot does, in one paragraph:** at every level it gathers food like the worker, strikes back at an enemy that hits one of its ants, sends a hit carrier home, keeps three fire walls in front of its thief hole when a thief threatens, puts out enemy fire walls and defuses enemy bombs near its hill and piles, and never sends its last ants into a fight; Medium and Hard take the Fire, Bomber and Thief power-ups of their own side at the start, send ants to the contested middle of the map (with six or more ants), keep a Combat Ant that harvests and fights, and raid the leading team's hill with a Thief; Hard steals an unguarded Thief power-up for a second thief and guides every carrier at the hill's gate by hand; four styles (aggressive, economic, raider, defensive; Hard plays aggressive or raider only) are drawn per match or pinned.

## Built

| Item | Where |
|---|---|
| **The standard bot** (kind `standard`, three levels, four styles): the economy of the worker plus the tasks below; `plan_for(level)` and `plan_for(level, style, rng)` hold every switch and number of a level and a style | `include/ants_ai/standard_bot.hpp`, `standard_tasks.hpp`, `tactics.hpp`; `src/ants_ai/standard_bot.cpp`, `standard_tasks.cpp`, `tactics.cpp` |
| The tasks: `FightTask` (strike back, rank 5), `CarrierAidTask` (a hit carrier is sent home), `WallTask` (the three fire walls of the thief hole, the counters to enemy fire), `BombTask` (defuse or set off enemy bombs), `PowerUpTask` (pick-up trips), `RaidTask` (raids; the ambush, off), `GateTask` (guiding for eating, Hard), `HarassTask` (the squad, Aggressive style), `SabotageTask` (a stolen Fire Ant walls in the best opponent's gate, Hard Aggressive), `StrikeTask` (Hard Aggressive), `HatchTask` and `GuardTask` (off) | `standard_tasks.hpp` / `.cpp` |
| The task model's additions: ranks in the `AntLedger` (`set_rank`, `take`: a task takes an ant from a task of a lower rank and from no other), `HarvestTask` options that only the standard bot turns on (typed ants harvest between jobs, fire-aware piles, the contest options, the gate's per-pile cap, the opening's minimum of six ants) and a two-way sync with the ledger; the worker's numbers are unchanged (AI3.12 and the pinned baselines pass untouched) | `tasks.hpp`, `tasks.cpp` |
| The view: `powerups()`, `powerup_at()`, `standing()`, `bombs()`, `fire_walls()` (`PowerUpView`, `BombView`, `FireWallView`), `MapInfo::cost_field_onto` | `bot_view.hpp` / `.cpp`, `map_info.hpp` / `.cpp` |
| The controller's two new filters and `Orders::pick_up` (see "Fairness surfaces") | `bot.hpp`, `bot_controller.cpp` |
| Styles: `Style`, `BotSpec::style`, the `--bot` forms `LEVEL:STYLE` and `KIND:LEVEL:STYLE`, the setup check, `make_bot`, the draw from the seat's own generator; the server's room specification (`"bot": "hard:raider"`) and the status rows (`style`) | `bot.hpp` / `.cpp`, `include/ants_server/room.hpp`, `src/ants_server/room.cpp`, `control.cpp` |
| The arena: `ArenaSeatResult::style`, `ArenaSpec::extra_kinds`; `bot_arena` kinds and options (`--tune`, `--ally-standard`, `--ally-pairs`, `standard+K=V`, `BOT_DIAG`); the bench bots that are never in the registry (`aggressor`, `aggressor2`, `rusher`, `saboteur`, `aggr1` .. `aggr9`, the diagnostics wrapper) | `arena.hpp` / `.cpp`, `tools/bot_arena.cpp`, `tools/bench_aggressor.hpp` |
| Tests: the cases AI7.1 - AI13.4 in seven files (`test_ai_b41.cpp`, `_team`, `_fight`, `_gate`, `_style`, `_offence`, `_cost`) and `b41_helpers.hpp`; S3.65 (the server's status rows); the render test of enemy bombs | `tests/test_ai/`, `tests/test_server/test_server.cpp`, `tests/test_app/test_render_parity.cpp` |
| The text under the bots' choice ("Bots gather food, raid and fight back.") | `src/ants_app/start_menu.cpp`, `web/lobby.html`, `README.md`, the two tests that pin it, four fingerprints |
| Documents | `docs/BOTS.md` ("The standard bot", "Measurements", the levels, interfaces, running bots, limits), `README.md`, `docs/NETWORK_PORT.md`, `docs/GAME_REVERSE_ENGINEERING.md` (three corrections), this file |

## The metric and the opponents

The prompt of B4-1 measured the bot by the economy (A1 - A6 below). In the middle of the work the owner played the worker on the beta site and the decision changed (the coordinator, 2026-10-03): **every conflict tactic is judged by the win rate (the seat's score strictly above the best of the other seats; in 2 + 2 the best of a pair against the best of the other pair) and the margin (own score minus the best other score), with the rank, per seed and then as a mean with its standard error over the seeds; the score is secondary**, against **conflict-rich opponents**: mirrors of the bot itself with all conflict tactics on, the bench aggressor, the double-thief aggressor, the centre rusher, the plain standard bot, in free-for-alls and in 2 + 2 alliances; **TREASURE first**. The economy tournaments stay as a sanity check and as the acceptance of the prompt. The bench bots are in `tools/bench_aggressor.hpp` and are arena-only kinds; the aggregation scripts (`exp.py`, `final_tables.py`) are research tooling and are not in the repository: the keys below reproduce every row with `bot_arena`.

Method: `bot_arena` of the release build, the arena's sink latency of 3 ticks, the opening of the product (`start_hold` 1), every distinct arrangement of the seats (`--rotate`), seeds 1 to 12 for the acceptance tables and 12 to 48 for the others (the number is in every table), the style of a bot drawn from the seat's generator unless a row says it is pinned. The final tables were played with an arena built from the tree before the last two commits (they rename two loop variables and add a test; the bots are the same).

## Acceptance of the prompt (A1 - A6), on the five maps with food on foot (ISLANDS was 0 for every bot until B4a: `docs/BOTS.md`, "Islands")

**A1, alone against three idle bots** (mean score of the standard bot / of the worker over the whole match, every seat, seeds 1 - 12, the percentage): acceptance at least 97 percent, **met at every map and level** (the lowest cell is TREASURE Medium, 99.0 percent: the trips of the opening).

| Map | Easy: standard / worker | Medium | Hard |
|---|---|---|---|
| TINY | 1539 / 1540 (100.0%) | 1790 / 1793 (99.8%) | 1996 / 1822 (109.6%) |
| SMALL | 1985 / 1981 (100.2%) | 2022 / 2020 (100.1%) | 2250 / 2030 (110.9%) |
| MEDIUM | 1791 / 1791 (100.0%) | 1802 / 1799 (100.2%) | 2031 / 1803 (112.7%) |
| GAUNTLET | 842 / 841 (100.1%) | 845 / 846 (99.9%) | 883 / 845 (104.4%) |
| TREASURE | 2780 / 2779 (100.1%) | 2924 / 2953 (99.0%) | 3651 / 2975 (122.7%) |
| ISLANDS | 0 / 0 | 0 / 0 | 0 / 0 |

**A2, two standard against two worker bots of the level**: acceptance, Medium and Hard higher than the worker on every one of the five maps, Easy at least 97 percent: **met, except that Medium ties on TINY and SMALL** (12 seeds: 100.4 and 100.0 percent; with 48 seeds 100.2 and 100.0, group wins 50.0 and 50.2). The reason is the economy: at Medium on those two maps there is no power-up to walk to and no fight, so the standard bot has nothing to do that the worker does not, and its only economic edge, the gate, loses there in two against two (below). Before the opening's minimum of six ants Medium was **99.1 and 98.8 percent** there (the contest of the middle cost a team of three or four ants 4 percent; the group win rate was 48.8 and 39.6): that was a real loss, now removed, not noise.

| Map | Easy: standard / worker (group wins) | Medium | Hard |
|---|---|---|---|
| TINY | 1207 / 1206 (100.0%; 46%) | 1205 / 1200 (100.4%; 50%) | 1255 / 1154 (108.8%; 83%) |
| SMALL | 753 / 755 (99.7%; 50%) | 752 / 752 (100.0%; 51%) | 806 / 696 (115.7%; 84%) |
| MEDIUM | 1227 / 1224 (100.2%; 50%) | 1252 / 1196 (104.7%; 62%) | 1346 / 1084 (124.2%; 92%) |
| GAUNTLET | 375 / 375 (99.9%; 45%) | 503 / 241 (209.2%; 54%) | 644 / 83 (775.9%; 100%) |
| TREASURE | 2216 / 2216 (100.0%; 46%) | 2818 / 1577 (178.7%; 100%) | 3366 / 911 (369.4%; 100%) |
| ISLANDS | 0 / 0 | 0 / 0 | 0 / 0 |

**The gate at Medium** (an option that is off; Medium with `gate=1,gatelat=27`, 24 and 36 seeds): alone TINY +0.5 percent, SMALL +2.1, MEDIUM +2.7, GAUNTLET +3.1, TREASURE +9.6; two against two against workers TREASURE 3,066 points against 2,875 (margin +406), MEDIUM +58, GAUNTLET +38, TINY 47.9 percent of the group wins against 50.0 (margin -23 against +2) and SMALL 45.4 against 50.2. It breaks the tie nowhere and loses on the two small maps, and it is Hard's by decision (the coordinator); the numbers are here for the day Medium is made stronger.

**A3, the ladder** (mean score over the five maps): alone Easy 1,788, Medium 1,877, Hard 2,162; two against two Easy 1,155, Medium 1,306, Hard 1,484: **Hard >= Medium >= Easy, no per-map inversion**. In conflict the ladder is steeper (below): a Hard bot wins 83.7 percent against three Medium bots, a Medium bot 0.7 percent against three Hard bots, an Easy bot 0.0 percent against three Medium bots, and the ladder holds for every style.

**A4, against the scripted aggressor** (the bench aggressor at Hard and two idle bots, 12 seeds, 12 arrangements; the score that the bot keeps, its percentage of its own alone score, and the worker beside it): acceptance, the standard bot keeps more than the worker. **Met at Medium and Hard on every map; at Easy a tie on average** (35.1 against 34.5 percent): lower than the worker on TINY and SMALL (35.8 against 38.7 and 45.3 against 48.2; with 48 seeds 37.0 against 39.3 and 45.3 against 48.0), higher on MEDIUM and GAUNTLET (18.9 against 16.0 and 40.3 against 34.9). Easy's one defender is fed to a Hard aggressor's Combat Ants where the map has nothing else to do; with `defenders=0` Easy would keep 621.8 and 960.4 points on TINY and SMALL (the worker 598.9 and 950.2), 285.7 and 312.4 on MEDIUM and GAUNTLET (289.9 and 284.9), a tie or a gain everywhere; with two defenders 552.1 and 877.5 (TINY, SMALL). Not changed: Easy is the level that stays what it is, and the single defender pays on the two maps where there is something to fight for. TREASURE: Medium keeps 442 points (15.1 percent) where the worker keeps 34, Hard 2,267 (62.1) where the worker keeps 24.

| Map | Easy: standard / worker | Medium | Hard |
|---|---|---|---|
| TINY | 551 (35.8%) / 596 (38.7%) | 899 (50.2%) / 814 (45.4%) | 1436 (71.9%) / 800 (43.9%) |
| SMALL | 899 (45.3%) / 954 (48.2%) | 1110 (54.9%) / 971 (48.1%) | 1729 (76.8%) / 943 (46.4%) |
| MEDIUM | 338 (18.9%) / 287 (16.0%) | 658 (36.5%) / 411 (22.8%) | 1238 (60.9%) / 310 (17.2%) |
| GAUNTLET | 339 (40.3%) / 293 (34.9%) | 396 (46.8%) / 314 (37.1%) | 595 (67.4%) / 304 (35.9%) |
| TREASURE | 20 (0.7%) / 24 (0.9%) | 442 (15.1%) / 34 (1.1%) | 2267 (62.1%) / 24 (0.8%) |
| ISLANDS | 0 (0.0%) / 0 (0.0%) | 0 (0.0%) / 0 (0.0%) | 0 (0.0%) / 0 (0.0%) |

**A5, whole matches** (four standard bots at every level on every map, 12 seeds, `--repeat 2 --replay-check`): **no command filtered or rejected**, the busiest seat needs 0.00 to 0.07 commands a second at Easy, 0.02 to 0.25 at Medium and 0.38 to 1.75 at Hard (budgets 0.4, 1.5 and 3.0), every match repeats bit for bit and its commands alone replay to the same hash at every 20th tick without any bot. In the suite: AI13.2 (the budget in every window of releases for TREASURE at the three levels and TINY at Medium and Hard with four seeds each, the filter, the repeat, the replay).

**A6, cost** (`AI13.1`, three runs on a quiet development machine, release build): the time of one look, `BotView::build` plus `think`, mean of 400 looks of a warm bot at a still world after 3,000 ticks, standard (worker), the three levels differ by 0.1 to 0.5 microseconds: TINY 0.8 - 1.2 (0.6 - 0.8), SMALL 1.1 - 1.4 (0.9 - 1.0), MEDIUM 2.1 - 2.7 (1.9 - 2.1), GAUNTLET 2.3 - 2.6 (2.1 - 2.4), TREASURE 3.2 - 3.7 (2.8 - 3.3), ISLANDS 3.1 - 3.8 (2.7 - 2.9). The whole of a TREASURE match (14,400 ticks, four bots of one kind; milliseconds idle / worker / standard): Easy 131 / 250 / 256, Medium 144 / 262 / 322, Hard 209 / 320 / 596; the bots' own part is 119 / 118 / 111 ms for workers and 125 / 178 / 387 ms for standard bots, so a look in a real match costs 27 microseconds at Hard where the warm look costs 3.5 (it carries the searches of the pick-up trips, the raids and the squad). The server (`S3.71`, twelve rooms of one person and three bots, only the server's own thread timed; ms per second of play for 12 rooms): TINY Hard standard 2.73 against worker 2.09 and idle 1.56; TREASURE Hard standard 15.62 against worker 9.75 and idle 8.14 (1.6 times the worker, **within the twice that the prompt allows**; the bots' own part is 7.5 ms against 1.4 ms, 0.6 ms per room and second, three quarters of one percent of the thread); TINY Medium standard 2.07 against worker 1.81; the worst single pass 14.0 ms (the test's bound is 250 ms).

## Conflict tournaments (TREASURE)

The tables of the owner's questions: does a tactic win more when the opponents fight? Hard styles and Medium styles first, then what each tactic does, then the conflict tactics that are off.

**Hard against Medium, Medium and Easy** (24 seeds, every arrangement of the seats; "the floor": every Hard style must win at least 80 percent with a clearly positive margin):

| Candidate | n | win rate (percent) | margin | rank | score (best other) |
|---|---|---|---|---|---|
| Hard aggressive | 288 | 88.4 +- 1.7 | +416 +- 23 | 1.1 | 2760 (2344) |
| Hard raider | 288 | 95.8 +- 1.0 | +526 +- 23 | 1.1 | 2902 (2376) |
| Hard, style drawn | 288 | 91.8 +- 1.3 | +502 +- 21 | 1.1 | 2846 (2344) |

**Hard styles against each other** (36 seeds): the cross table, two bots of a style against two of the other (the best of a pair against the best of the other pair), and one bot of a style against three Hard bots that draw their style:

| Candidate | n | win rate (percent) | margin | rank | score (best other) |
|---|---|---|---|---|---|
| two aggressive against two raiders | 216 | 40.0 +- 2.7 | -71 +- 13 | 2.7 | 2152 (2374) |
| two raiders against two aggressive | 216 | 60.0 +- 2.7 | +71 +- 13 | 2.3 | 2246 (2303) |
| one aggressive against three Hard (drawn) | 144 | 16.0 +- 3.2 | -291 +- 25 | 2.8 | 2134 (2425) |
| one raider against three Hard (drawn) | 144 | 30.2 +- 4.1 | -147 +- 22 | 2.2 | 2256 (2402) |

**Against the bench aggressor** (12 seeds; the aggressor and two idle bots; the own score kept, the alone score is 3,651 at Hard):

| Candidate | n | win rate (percent) | margin | rank | score (best other) |
|---|---|---|---|---|---|
| Hard aggressive | 144 | 95.8 +- 1.6 | +2029 +- 48 | 1.0 | 2101 (72) |
| Hard raider | 144 | 93.8 +- 2.3 | +2021 +- 86 | 1.1 | 2084 (64) |
| Hard, drawn | 144 | 94.4 +- 1.9 | +2205 +- 49 | 1.1 | 2266 (62) |
| Hard, drawn, against three Medium (36 seeds) | 144 | 83.7 +- 2.6 | +272 +- 17 | 1.2 | 2547 (2275) |

**Medium styles** (TREASURE; "round robin" is one style against the three others, 12 seeds; "mix" against three Medium bots that draw theirs, 36 seeds; "Easy" against three Easy bots, 24 seeds; "aggressor" against the bench aggressor and two idle bots, 12 seeds):

| Style | round robin: win rate / margin | mix: win rate / margin | against three Easy | against the aggressor: win rate / margin (own score) |
|---|---|---|---|---|
| aggressive | 19.3 +- 1.9 / -238 | 19.8 / -216 | 96.9 / +533 | 31.6 / -25 (358) |
| economic | 28.5 +- 2.2 / -164 | 25.0 / -163 | 80.2 / +290 | 33.3 / -34 (342) |
| raider | 25.3 +- 1.9 / -205 | 23.3 / -208 | 83.3 / +324 | 42.4 / +154 (490) |
| defensive | 26.9 +- 1.3 / -179 | 29.2 / -168 | 71.9 / +274 | 42.0 / +132 (438) |
| drawn | | | 78.6 / +366 | 39.6 / +91 (442) |

The ladder: a Medium bot (drawn) against three Hard bots wins 0.7 percent (margin -681); an Easy bot against three Medium bots 0.0 percent (-996); four Easy bots 25.0 percent (the symmetric result).

**What each tactic does to a Hard bot** (the neutral plan of Hard with one change; the columns are four benches; 25.0 in the mirror is the symmetric result):

| Hard with ... | against Medium, Medium, Easy (24 seeds) | against three plain Hard (mirror, 48 seeds; 25.0 is symmetric) | against the aggressor, 12 seeds: win rate / margin (own score) | against aggressor, rusher, plain bot (12 seeds) |
|---|---|---|---|---|
| the shipped plan (neutral, no style) | 96.7 / +515 | 25.0 / -218 | 95.1 / +2197 (2251) | 60.4 / +180 |
| no Combat Ant | 96.2 / +522 | 35.9 / -122 | 85.4 / +1028 (1191) | 48.8 / +7 |
| raw harassment squad of Combat Ants | 76.4 / +268 | 7.3 / -494 | 81.9 / +1011 (1209) | 48.4 / +8 |
| Combat Ant parked on a guard post | 81.4 / +224 | 10.9 / -369 | 88.9 / +1395 (1493) | 40.3 / -129 |
| squad, reach 14 | 89.6 / +412 | 4.9 / -411 | 95.8 / +2112 (2165) | - |
| squad that learns, reach 8 (the Aggressive style) | 94.3 / +434 | 13.0 / -344 | 94.4 / +2143 (2202) | 60.6 / +114 |
| squad that hatches for itself | 37.7 / -146 | 0.0 / -1246 | - | - |
| sabotage with a stolen Fire Ant | 96.4 / +561 | 25.0 / -218 | 94.4 / +2114 (2178) | - |
| ambush at a thief hole | 96.7 / +530 | 18.8 / -269 | - | - |
| strike when behind | 97.0 / +517 | 25.0 / -219 | - | - |
| one Thief instead of two | 95.7 / +469 | 25.0 / -219 | - | - |
| no raids | 74.5 / +155 | 21.1 / -278 | - | - |
| no contest of the middle | 96.0 / +517 | 34.4 / -154 | - | - |
| one ant to the middle | 95.5 / +484 | 25.5 / -222 | - | - |

**The conflict tactics that are off** (a mirror of four standard bots that all play the tactic, against the shipped plan, 36 seeds):

| The conflict tactics | Medium (36 seeds): win rate / margin | Hard (36 seeds) |
|---|---|---|
| all on (the candidate is one of four all-on bots: 25.0 is symmetric) | 25.0 / -403 | 25.0 / -384 |
| the shipped plan (all off) against three all-on bots | 39.9 / -153 | 50.3 / -51 |
| all on except the strict contest order | 40.6 / -180 | 45.1 / -94 |
| all on except strikes | 25.0 / -398 | 24.3 / -388 |
| all on except hatching for fights | 25.7 / -345 | 26.7 / -333 |
| all on except the wipe-out focus | 25.0 / -403 | 25.0 / -384 |
| shipped plan plus the strict contest order | 25.7 / -351 | 26.4 / -338 |
| shipped plan plus hatching for fights | 39.9 / -181 | 45.1 / -92 |
| shipped plan plus strikes | 40.6 / -156 | 49.7 / -54 |

**The opening's contest of the middle, by the number of ants** (Medium A2 against workers, 24 seeds; whole-match duels of two bots against two plain bots, 36 seeds; win rate / margin): TINY (3 ants): one ant 45.5 / -49 against none 50.0 / +3 and two ants 49.3 / -49 against workers; in the duel against the plain bot the bot with no contest wins 53.7 / +52 (Medium, against one ant) and 81.7 / +51 (Hard, against two); SMALL, MEDIUM and GAUNTLET are identical with and without it; TREASURE (6 ants): against workers 2,862 / 2,881 / 2,840 points with none / one / two ants (margin +1,416 / +1,429 / +1,362), in the duels 53.0 / +1 and 53.0 / +12 at Medium, and at Hard 56.5 / +56 for none and 52.5 / +6 for one ant against the plain bot's two. Decision: fewer than six ants send nobody; TREASURE keeps one (Medium) and two (Hard) by the coordinator's decision (the owner expects to see the centre contested), and **the cost of Hard's two ants on TREASURE, about 5 percent of the margin of a whole-match duel, is written down here**.

**Combat Ant and the mirror.** The Combat Ant that harvests costs about eleven points of win rate where nobody attacks (Hard mirror 25.0 against 35.9 percent without it, margin -218 against -122) and is worth 11.6 points against a mixed bench of aggressor, rusher and plain bot (60.4 against 48.8) and doubles what an aggressor leaves (2,251 against 1,191 points, win rate 95.1 against 85.4). It stays (the coordinator: "both keep the harvesting Combat Ant"); the obvious next step is to take it when the bot is attacked or sees a Combat Ant or a Thief (`combat_when_attacked` and `fists_strict` are in the plan; they were measured when the Combat Ant was a guard and not again with the Combat Ant that harvests), which is a measurement for B5, not a change for B4-1.

## Where this differs from the design, and why

- **No guard post.** The design had Medium and Hard park a Combat Ant where its reflex covers the hill. Measured: a parked Combat Ant is an ant that does not harvest (alone it costs 8 to 11 percent) and loses the mirror (Hard 81.4 percent against 96.7 against Medium, Medium and Easy; 10.9 against 25.0 in the Hard mirror). The Combat Ant harvests and its reflex punches what comes near. The guard task stays behind `plan.guards`.
- **No interception of thieves, no hatching, no wipe-out focus, no strict contest order, no ambush**: each measured as a loss or as inert (`docs/BOTS.md`, "Aggression"); each stays in the code behind a flag with its test, for the next round. The strike and the sabotage are part of the Hard Aggressive style only.
- **The tactics column of the levels table** (swimmer ferry, island hops, shore kills, alliance invitations): the island hops and the ferry are B4a and were not built in B4-1 (built since: `docs/BOTS.md`, "Islands"); a bot never invites (the owner's rule stays: B4-1 keeps the one Deny where it denies and accepts by a rule where that is safe); shore kills were not measured.
- **Styles are an addition** (the owner's request after the first measurements): four styles, drawn per match, pinned by `--bot`, never shown in the name; Hard plays aggressive or raider only.
- **The metric changed in the middle** (the coordinator's decision of 2026-10-03, after the owner played): every conflict tactic was judged by the win rate and the margin against opponents that fight, not by the score against passive ones. Several tactics lowered the score against workers and raised the win rate against fighters (the opening's contest at TREASURE is a tie against workers and what a person expects to see; the Combat Ant costs eleven points of win rate in the Hard mirror and halves what an aggressor takes); others the other way round (the raw squad of the first design won 76 percent against Medium and Easy bots where the shipped plan wins 97, and 7 percent in the Hard mirror where the symmetric result is 25). The economy tournaments (A1 to A5) remain as a sanity check and as the acceptance of the original prompt.
- **Opening with six ants.** TINY (3 ants) and SMALL (4) do not contest the middle at the start: it cost Medium 4 percent of its score against workers on TINY and lost the duels there (Hard with no contest won 81.7 percent of its duels against Hard with two ants).

## Fairness surfaces: what the standard bot reads and what the controller refuses

The rule (project rule 8): a bot reads what a person of its seat sees and clicks what a person can click. B4-1 added these surfaces and checked each against the screen:

| New | What it exposes | Why it is fair |
|---|---|---|
| `BotView::powerups()`, `powerup_at()` | every power-up on the map now (tile, kind, the team and ant that STANDS on it) | power-ups are drawn on every screen; a standing ant is an ant on a power-up that is not walking (visible); the dropper's clock is not exposed (a droplet that has not landed is not in the list) |
| `BotView::standing()` | whether an ant is immune | derived from the drawn state and the tile |
| `BotView::bombs()` | every bomb with the team whose colour it is drawn in | the renderer draws every bomb, an enemy's too, in the owner's colour (a render test pins that every viewer sees every owner's bomb); with Fog of War a bomb on an unexplored tile is hidden, and bots are refused with fog |
| `BotView::fire_walls()` | tiles only | a fire wall is drawn alike for every owner; who lit it and how long it still burns are on no screen and are not in the list (`grid()` still lends them: the accepted deviation of `docs/BOTS.md`) |
| Controller: an **attack** needs an enemy ant (another team, not an ally) on its tile, and the tile is not a hill tile | the HUD sends a group attack only from the attack cursor, which shows over an enemy ant, and an ant on a hill tile gets the plain move cursor | a bot can no longer propose an attack that no click can produce |
| Controller: a **move onto a power-up tile** passes only as a planned pick-up (`Orders::pick_up`, ONE ant, a power-up on the tile); no special order or attack names a power-up tile | a click on a power-up tile takes it for the ant that arrives | no group order, rally or spread can name one by accident (it changed one existing test: its walkers now avoid power-up tiles) |
| `Memory` (the bot's own, soft): hits on its own ants, who was drawn attacking, who has been seen playing | built from the bot's own ants' hit points (which it knows) and the drawn state of the others (what is on the screen) | nothing in it is another team's hit points, carried points, eggs or orders; AI1.25 (two engines that differ only in what other teams hide give identical views) still passes with the new members included |

## Findings about the engine (verified; the tests pin them)

- **Three fire walls stop a raid.** A thief reaches the raid tile `(bx + 3, by + 2)` only by stepping from `(bx + 4, by + 1 .. by + 3)`; with a wall on each the engine's raid order ends in "Can't go there." and the victim keeps its points; two walls, or three one tile further east, let it through (AI7.12). A wall lives exactly 3,600 ticks; the owner's deposits are unaffected; the first wall comes 67 ticks after the order from four tiles away, then 51 and 41. All three east tiles are fire-OK on every shipped map.
- **Power-ups.** Reachable Fire power-ups on foot exist on TREASURE only (four, 185 to 211 ticks from the hills' queues), the flower droppers of MEDIUM, GAUNTLET and SMALL drop one in 25, 40 and 45 percent of their drops (ISLANDS 5 percent, unreachable). An ant standing on a power-up is immune: an attack order is acknowledged and then ends in "Can't go there." three or four ticks later; a Combat Ant's reflex fails the same way. A group move onto a tile that an enemy ant occupies is also an attack (`classify_order`: the occupant first); a click on a power-up tile with no ant on it is a plain move that takes it.
- **Typed ants harvest like workers** (3,000 ticks alone: worker 350 points, Thief and Swimmer 350, Bomber, Fire and Combat 325); a harvesting Combat Ant's reflex punched an enemy worker near its route (10 to 6 hit points) and it went on harvesting. A harvesting thief is never idle (the loop is the engine's), so a raid that waited for an idle thief never started (the bug AI7.25 pins).
- **Fights.** One attack order is one blow (the attacker is idle after it); a worker loses 1 hit point, a Combat Ant's blow takes 2; the victim is thrown 1 or 4 tiles and its order is cleared, so a hit carrier stands idle with its food until it is clicked; at 1 hit point an ant retreats home; a lone worker 25 tiles from its hill survived three, five and eight worker attackers; a lone Combat Ant kills a worker in about 300 ticks (54 ticks per blow, five blows). A blow throws a victim a tile or two: later orders at "the same place" have to allow for it.
- **Hatching** (R3): the starting eggs per team are TINY 3, SMALL 2, MEDIUM 6, GAUNTLET 6, TREASURE 9, ISLANDS 4 and the starting ants TINY 3, SMALL 4, MEDIUM 6, GAUNTLET 6, TREASURE 6, ISLANDS 8. A click costs `min(200, score)` and is refused below 200; the newborn exists 160 ticks later and takes orders at 171 (247 at the entrance tile: never park an own ant on it, births wait for it). **The forced hatch** (`remove_ant`, `Ants.exe` `CheckNoAnts` `0x100cf48`) is real and is NOT free: when the last ant of a team is REMOVED (the end of its death clip, about 42 ticks after the lethal blow; hit points 0 alone do nothing) and it has an egg and nothing hatches, an egg starts in the same tick for `min(200, max(0, score))` (a score of 0 still loses the egg); no egg left means the team is out. A wipe costs the victim at most 200 for each egg it owns. A bot cannot see another team's eggs. Corrections made in `docs/GAME_REVERSE_ENGINEERING.md`: the forced hatch was written down as free, and the emergence invulnerability of section 17 is unsupported (the hatch task never creates it).
- **The gate** (R1): the engine's queue caps a hill at 10 to 13 deposits a minute (a gap of 93 to 116 ticks; the entrance is busy 35 ticks per deposit, the waiting tile `(bx - 1, by + 3)` is on the far side of the mound and the queued ant is sent on a walk of 44 to 68 ticks around it); a click on the entrance is redirected to the ring while the entrance is occupied or claimed or any own ant is queued; two deposits can never overlap. The rules match the original (`Ants.exe` `FUN_0100ff1f`, `FUN_0101f780`, `FUN_010202e7`), so the model that every table rests on is the original's (the owner asked whether ants pile up at the entrance in the original: they do, by the same rules). Hand-driven with a Hard-limited client a deposit takes 55 to 65 ticks (18.5 to 19.5 a minute): +55 to +86 percent for 8 to 12 workers on a pile 8 to 20 tiles away, +21 to +82 at 6 workers, 70 to 91 percent of the ideal, 0.4 to 1.05 commands a second. Whole-match gains of the researched policy: TINY +4, SMALL +11, MEDIUM +6, GAUNTLET +2, TREASURE +23 percent. A seat alone is gate-limited (TREASURE 2,962 points = rate times length), four teams exceed the pot (8,900), so a full table is pot-limited, and a 16th or 20th worker adds nothing.
- **Mud humping** (R2): the engine reproduces the effect exactly and so does the original (`go_to` snaps the ant to the centre of the tile it is on, the same logic as `GoTo` `0x101fc50`: snap `0x101fcce` .. `0x101fcf6`, the same-tile skip `0x102880d` .. `0x10288dc`, the first move 270 ms after the restart). The gain is small, only on mud (grass loses 1 tick), and needs a click in a window of 3 ticks after the tile edge: +3 (east, south), +2 (west, north), +5 (SE, NE, SW) or +4 (NW) ticks per tile (mud 18 ticks a tile orthogonally, 24 diagonally; perfect clicks 15.8 per tile instead of 18.5), and an order outside the window loses (up to -14 before it). A Hard bot (a look every 4 ticks, the order leaves 6 to 10 ticks later) cannot hit it: reacting to a look loses 61 to 97 ticks per 11 tiles, a timed open loop gains at most 1 to 1.5 ticks per mud tile on diagonal runs. Mud is 0 percent of the walking of TINY, SMALL and ISLANDS, 8.2 on MEDIUM, 4.9 on GAUNTLET and 3.9 on TREASURE (9.8 for the seat that crosses the causeway of 3-tile mud runs): at most about 1.5 percent of the walking time for a click-perfect person. **Not built; the engine matches the original, so there is no fidelity question.** The document's old bullet was wrong and is corrected.
- **Economy** (R3): hatching never pays (solo Medium, never hatching / hatching whenever: TINY 1,791 / 1,396, SMALL 2,020 / 1,830, MEDIUM 1,802 / 632, GAUNTLET 846 / 86, TREASURE 2,962 / 1,035; one hatch: TINY +28, SMALL -123, MEDIUM -197, GAUNTLET -154, TREASURE -232). The standard bot against the bench aggressor suffers 2.7 forced hatches a match on TREASURE.

## Tests, existing tests that changed (and why), texts that changed

**New:** 59 cases in suite 2.20 (`test_ai`; after the review AI2.21, AI7.27 - AI7.29, AI10.4 - AI10.7 and AI13.4 came with H1, M1 and M2), in `tests/test_ai/test_ai_b41.cpp` (AI7.1 - AI7.26), `test_ai_b41_team.cpp` (AI8.1 - AI8.6), `test_ai_b41_fight.cpp` (AI9.1 - AI9.5 and AI9.4b), `test_ai_b41_gate.cpp` (AI10.1 - AI10.3), `test_ai_b41_style.cpp` (AI11.1 - AI11.12), `test_ai_b41_offence.cpp` (AI12.1 - AI12.3), `test_ai_b41_cost.cpp` (AI13.1 cost, AI13.2 acceptance A5, AI13.3 sixteen random worlds) and the helpers `b41_helpers.hpp` (a rig that drives a bot by hand, hand-made worlds); S3.65 extended in `test_server` (the style rows of a room's bots; no new suite, so `run_tests.sh` and CI's list of suites are unchanged); in `test_render_parity` the check that every viewer sees every owner's bombs in the owner's colour. The tactics that are off are tested with their flag on, so that they stay correct for the next round (AI7.9 interception, AI7.16 the guard, AI8.2 and AI8.3 the contest order, AI9.2 - AI9.4 hatching, the strike and the wipe-out focus, AI12.1 - AI12.3 the squad, the sabotage and the ambush); the plans that ship are pinned (AI9.5, AI11.6).

**Existing tests that had to change, each because its premise was "standard is the worker until B4" or "a bot may click a power-up tile"** (nothing weakened, no assertion removed):
- `test_ai_setup.cpp`: the registry's `standard` is its own kind (`kind()` says "standard"; was "worker").
- `test_ai_arena.cpp` (AI4.1 and AI4.3): the scripted walker of the replay test never names a power-up tile (the controller now refuses a plain move onto one, so a match of walkers has no refused command; the random draws are unchanged), and the seat that runs the standard bot reports `runs == "standard"` (was "worker").
- `test_ai_worker.cpp` (AI3.14): "`standard` equals `worker`" is replaced by the same checks on the standard bot's own match: bit-reproducible, replayed without a bot, the hash differs from the worker's (the kind is part of the seat's seed).
- `test_ai_b2fix.cpp` (AI1.25, the non-interference test): `view_text` carries the new view members (power-ups with who stands on them, bombs with owners, fire walls) and the two worlds that differ only in what other teams hide now also hold power-ups, bombs and fire walls with different timers, so that the test would catch a leak through the new members (three `static_assert`s remind the next author that the view structs changed).
- `test_server.cpp` (S3.65): the rows of the status JSON carry `style` ("random" for a bot that draws its own); a room with pinned styles is added (so three rooms became four); a style that the level may not play, a bot that has none and a word that is no style are refused with 400.
- `test_start_menu.cpp` (M4.3), `test_view_fingerprint.cpp` (three rows), `test_wide_pages.cpp` (one row), `tests/scripts/test_ants_server.sh` (the web page's caption): the line under the bots' choice now says "Bots gather food, raid and fight back." (it said "Bots gather food; they do not fight yet."); the four fingerprints moved for that one string (the draw-call counts are the same) and the new line fits the 350 px rectangle at Px14 (the first wording, 56 characters, did not).
- Comment-only: `test_network_app.cpp`, `test_ai_main.cpp` / `ai_test.hpp` (registration of the new groups), `tests/test_ai/CMakeLists.txt` (the new sources and the include path of `tools/` for the bench bots).

## Mutation check (the later steps; the first 71 mutants of step 2 are in the commit history)

Each deliberate fault was put into a scratch copy of the sources (the library objects and the test binary rebuilt every time) and the test that should catch it was run (the whole suite for the survivors, which found that another test owned several of them); "caught" is a failing test. Five rounds on the later steps (the first 71 mutants of step 2, the walls, counters, pick-ups, guard and raids of the first commits, are in the history of this branch): the first 81 mutants found 10 that did not build under `-Werror` (unused variables: made to build), 12 survivors that the whole suite re-ran (several belong to other tests) and four real gaps; the tests that closed them (AI8.6 the help for the ally, AI7.24 the ally's Thief power-up, AI12.1 the best opponent's carrier and the ally's ants near a target, AI11.7 and AI11.10, AI7.26 the raid blacklist, AI12.2 the sabotage's minimum and its victim) were added and the mutants run again; round four (27 mutants of the fight package, the contest order and the gate) found five more gaps (AI9.2, AI9.4b, AI8.3), closed in round five. What is left is explained below the table: equivalent mutants (a guard that stands twice, a default that is overridden), and details and tuning constants of tactics that are off.

| Mutant | The fault | File | Result | Caught by |
|---|---|---|---|---|
| A01 | no ally fights | `standard_tasks.cpp` | caught | AI8.6 |
| A02 | counters for ally always | `standard_tasks.cpp` | caught | AI8.6 |
| A03 | ally fight no radius | `standard_tasks.cpp` | caught | AI8.6 |
| A04 | ally fight nobody near | `standard_tasks.cpp` | caught | AI8.6 |
| A05 | ally ants count as enemy | `standard_tasks.cpp` | caught | AI12.1 |
| A06 | hard defensive raider raid min | `tactics.cpp` | caught | AI11.7 |
| A07 | defensive no ally help | `tactics.cpp` | caught | AI11.10 |
| B01 | no adaptive combat | `standard_bot.cpp` | caught | AI11.11 |
| B02 | sabotage no second fire | `standard_bot.cpp` | caught | AI11.9 |
| B03 | no thief want | `standard_bot.cpp` | caught | AI7.24 |
| B04 | accept always | `standard_bot.cpp` | caught | AI8.1 |
| B05 | accept with ally | `standard_bot.cpp` | caught | AI8.1 |
| B06 | no memory attacked | `standard_bot.cpp` | caught | AI11.11 |
| B10 | sabotage keeper not spared | `standard_tasks.cpp` | caught | AI12.2 |
| B11 | sabotage ring short | `standard_tasks.cpp` | caught | AI12.2 |
| B12 | sabotage no min score | `standard_tasks.cpp` | caught | AI12.2 |
| B13 | sabotage ally victim | `standard_tasks.cpp` | caught | AI12.2 |
| F01 | last ants fight | `standard_tasks.cpp` | survived, explained |  |
| F02 | fight reserve no egg rule | `standard_tasks.cpp` | survived, explained |  |
| G01 | gate no slots | `standard_tasks.hpp` | caught | AI10.1 |
| H01 | no odds | `standard_tasks.cpp` | caught | AI12.1 |
| H02 | range ignored | `standard_tasks.cpp` | caught | AI11.9 |
| H03 | no retreat pause | `standard_tasks.cpp` | caught | AI11.9 |
| H04 | pause never doubles | `standard_tasks.cpp` | caught | AI11.12 |
| H05 | strong needs four | `standard_tasks.cpp` | caught | AI11.12 |
| H06 | no strong defence | `standard_tasks.cpp` | caught | AI11.12 |
| H07 | power up carrier target | `standard_tasks.cpp` | caught | AI12.1 |
| H08 | paused team targeted | `standard_tasks.cpp` | caught | AI11.12 |
| H09 | hurt member stays | `standard_tasks.cpp` | caught | AI11.9 |
| H10 | leader not preferred | `standard_tasks.cpp` | caught | AI12.1 |
| H11 | ally targeted | `standard_tasks.cpp` | caught | AI12.1 |
| K01 | min ants ignored | `tasks.cpp` | caught | AI8.5 |
| K02 | plan min zero | `tactics.hpp` | caught | AI8.5 |
| K03 | min not passed | `standard_bot.hpp` | caught | AI8.5 |
| K04 | cap ignored | `tasks.cpp` | caught | AI8.4 |
| P01 | medium no contest | `tactics.cpp` | caught | AI8.4 |
| P02 | hard one contest | `tactics.cpp` | caught | AI8.4 |
| P03 | hard one thief | `tactics.cpp` | caught | AI7.24 |
| P04 | hard no steals | `tactics.cpp` | caught | AI7.24 |
| P05 | hard no gate | `tactics.cpp` | caught | AI10.2 |
| P06 | hard no second combat | `tactics.cpp` | caught | AI11.6 |
| P07 | hard guards | `tactics.cpp` | caught | AI11.6 |
| P08 | hard combat late | `tactics.cpp` | caught | AI11.6 |
| P09 | hard raid min 30 | `tactics.cpp` | caught | AI11.7 |
| P10 | hard avoids guards | `tactics.cpp` | caught | AI7.15 |
| P11 | medium guards hill | `tactics.cpp` | caught | AI11.6 |
| P12 | medium no raids | `tactics.cpp` | caught | AI7.15 |
| R01 | raid no min | `standard_tasks.cpp` | caught | AI7.15 |
| R02 | raid poorest first | `standard_tasks.cpp` | caught | AI7.15 |
| R03 | raid shut hill | `standard_tasks.cpp` | caught | AI7.15 |
| R04 | raid walking thief not taken | `standard_tasks.cpp` | caught | AI7.25 |
| R05 | raid no round trip | `standard_tasks.cpp` | caught | AI7.15 |
| R06 | raid ally hill | `standard_tasks.cpp` | caught | AI7.15 |
| R07 | raid no blacklist | `standard_tasks.cpp` | caught | AI7.26 |
| S01 | hard allows every style | `bot.cpp` | caught | AI11.1 |
| S02 | parse no level check | `bot.cpp` | caught | AI11.2 |
| S03 | parse style for any kind | `bot.cpp` | caught | AI11.2 |
| S04 | setup no style check | `bot.cpp` | caught | AI11.2 |
| S05 | registry drops style | `bot.cpp` | caught | AI11.4 |
| S06 | pin ignored | `standard_bot.cpp` | caught | AI11.4 |
| S07 | draw ignores seed | `standard_bot.cpp` | caught | AI11.3 |
| S08 | hard always aggressive | `tactics.cpp` | caught | AI11.3 |
| S09 | draw first style | `tactics.cpp` | caught | AI11.3 |
| S10 | no variations | `tactics.cpp` | caught | AI11.3 |
| S11 | easy plays styles | `tactics.cpp` | caught | AI11.5 |
| T01 | hatch ally margin always | `standard_tasks.cpp` | caught | AI9.2 |
| T02 | hatch no score check | `standard_tasks.cpp` | caught | AI9.2 |
| T03 | hatch one blow is a fight | `standard_tasks.cpp` | survived, explained |  |
| T04 | hatch ignores refusal | `standard_tasks.cpp` | caught | AI9.2 |
| T05 | hatch no time left check | `standard_tasks.cpp` | caught | AI9.2 |
| T06 | hatch never lost ant | `standard_tasks.cpp` | caught | AI9.2 |
| T07 | hatch two at a time | `standard_tasks.cpp` | caught | AI9.2 |
| T08 | strike hurt members | `standard_tasks.cpp` | survived, explained |  |
| T09 | strike when not behind | `standard_tasks.cpp` | caught | AI9.3 |
| T10 | strike no reserve | `standard_tasks.cpp` | caught | AI9.3 |
| T11 | wipe odds one | `standard_tasks.cpp` | caught | AI9.4 |
| T12 | wipe any team size | `standard_tasks.cpp` | caught | AI9.4 |
| T13 | wipe never bigger | `standard_tasks.cpp` | caught | AI9.4 |
| T14 | behind by any margin | `tactics.cpp` | caught | AI9.3 |
| T15 | behind small leader | `tactics.cpp` | survived, explained |  |
| T16 | gate for everybody | `standard_bot.cpp` | caught | AI10.2 |
| T17 | gate keeps rescue | `standard_bot.hpp` | survived, explained |  |
| T18 | gate one staged | `standard_tasks.hpp` | survived, explained |  |
| T19 | gate no exit time | `standard_tasks.hpp` | caught | AI10.1 |
| T20 | gate late clip | `standard_tasks.hpp` | survived, explained |  |
| T21 | gate no predictive | `tactics.hpp` | survived, explained |  |
| T22 | contest ignores first | `tasks.cpp` | caught | AI8.3 |
| T23 | contest multi needs three | `tasks.cpp` | caught | AI8.3 |
| T24 | contest ally never near | `tasks.cpp` | caught | AI8.3 |
| T25 | contest tiers ignored | `tasks.cpp` | caught | AI8.2 |
| T26 | defenders one more | `tactics.cpp` | caught | AI7.6 |
| T27 | leash tiny | `tactics.cpp` | survived, explained |  |
| U01 | steal ally | `standard_tasks.cpp` | caught | AI7.24 |
| U02 | steal always | `standard_tasks.cpp` | caught | AI7.24 |
| U03 | own side not first | `standard_tasks.cpp` | caught | AI7.24 |
| V01 | style not listed | `server: control.cpp` | caught | S3.65 |
| V02 | style not copied | `server: room.cpp` | caught | S3.65 |
| V03 | pinned not in bot text | `server: control.cpp` | caught | S3.65 |
| Y01 | aggr contest | `tactics.cpp` | caught | AI11.9 |
| Y02 | aggr defender | `tactics.cpp` | caught | AI11.9 |
| Y03 | aggr range | `tactics.cpp` | caught | AI11.9 |
| Y04 | aggr medium odds | `tactics.cpp` | caught | AI11.9 |
| Y05 | aggr retreat | `tactics.cpp` | caught | AI11.9 |
| Y06 | aggr hard second fire | `tactics.cpp` | caught | AI11.9 |
| Y07 | aggr no squad | `tactics.cpp` | caught | AI11.6 |
| Y08 | aggr hard no sabotage | `tactics.cpp` | caught | AI11.6 |
| Y09 | econ contests | `tactics.cpp` | caught | AI11.8 |
| Y10 | econ raids small | `tactics.cpp` | caught | AI11.8 |
| Y11 | econ walls late | `tactics.cpp` | caught | AI11.8 |
| Y12 | raider neutral order | `tactics.cpp` | survived, explained |  |
| Y13 | raider takes bomber | `tactics.cpp` | caught | AI11.7 |
| Y14 | raider raids dear | `tactics.cpp` | caught | AI11.7 |
| Y15 | defensive walls late | `tactics.cpp` | caught | AI11.10 |
| Y16 | defensive no ally help | `tactics.cpp` | caught | AI11.10 |
| Y17 | defensive contests | `tactics.cpp` | caught | AI11.10 |

115 mutants in the table; 104 caught, 11 survived, explained.

The survivors, one by one:

- **Y12**: equivalent: the Raider does not secure the Bomber, so the place of the Bomber in the order of the trips changes nothing; the line was removed.
- **F01**: equivalent: the same guard stands again in run_fight, which ends a fight started by mistake in the same look (only a counter differs).
- **F02**: equivalent: idem.
- **T03**: equivalent in effect: hatching replaces lost ants, and a loss is a fight by itself; a blow alone leaves the count of ants at the start count.
- **T08**: a detail of a flag that is off (a member with fewer than 7 hit points joins the force).
- **T15**: a detail of a flag that is off (the leader must show at least 300 points before a strike).
- **T17**: equivalent in practice: the gate takes over every carrier before the economy's rescue (900 or 40 ticks idle) could fire.
- **T18**: equivalent: the default of Params is overridden by the plan (gate_max_staged).
- **T20**: a tuning constant (the enter clip is 22 ticks); a later guess only delays the predictive click, AI10.1 keeps its margins.
- **T21**: the predictive click (given a look's latency before the gate is seen free) is a refinement of the researched policy; AI10.1 pins its result (the gap between deposits and the score), which the plain click also reaches.
- **T27**: a tuning constant of the fight (the leash of Medium is 10 tiles); AI7.6 does not move an enemy beyond it.

## Bugs found on the way (all fixed, each with a test)

- **A harvesting Thief is never idle** (the loop of bite, walk home, deliver is the engine's), so the raids waited for a thief that never stood still and never started: the raid task takes a walking, empty-handed thief (AI7.25).
- **Medium stole its neighbours' Thief power-ups** (a hand-made world and a mutant showed it): stealing is Hard's alone, never the ally's, and the own side's power-up comes first (AI7.24).
- **The squad's `on_command` was not forwarded** in the first version: its orders left every 200 ticks instead of after every blow (found by the attack orders per match in the tables).
- **A thief that queued at the far ring of the gate poisoned the FIFO rule** (50 to 65 refused clicks a match): the gate treats loot as a carrier (AI10.2).
- **Interception of thieves, the parked guard, hatching and the strict contest order lost** in the conflict tournaments although each looked right in a hand-made world (tables above): off, flags and tests kept.
- **The contest of the middle with three ants** cost TINY 4 percent against workers and lost the duels (AI8.5).
- **Two loop variables called `id_` hid `Task::id_`** in the strike and the squad: clang (macOS) does not warn, GCC 12 with `-Werror -Wshadow` stops the build. Found by the GCC 12 gate before anything was pushed; renamed (the only GCC-only finding of the batch).
- **`bot_arena`'s usage text lost the end of its `--tune` line** to a bad edit (found by the fast tier after the texts changed): fixed.
- **Documents were wrong** (`docs/GAME_REVERSE_ENGINEERING.md`): the mud "humping" bullet (3 << 16 is in neither the program nor the remake), the forced hatch is "free" (it costs `min(200, score)`), the emergence invulnerability of section 17 (the hatch task never creates it). Corrected there.

## B4a: the island play (the deep tier; numbers in `docs/BOTS.md`, "Measurements")

**What the measurements decided.** (1) No builder in the level plans: two bots of the plan against two of today's on SMALL (32 matches a level) differ by -94, +21 and +82 points at Easy, Medium and Hard with a builder and by +163, +220 and +195 without; a Swimmer that carries food earns 700 to 850 points a match, a bridge carries one ant at a time. The code stays (tests, `--tune ibuild=1`) and guards the bridges that others dig. (2) The wants (Easy 2, Medium 3, Hard 3) hold on SMALL as well: 1, 2 and 3 Swimmers gave +132, +176, +176 at Easy, +209, +237, +238 at Medium and +172, +210, +214 at Hard. (3) Walking the crew over bridges to the rows was not built: no bridge reaches a corner before the flights have brought the Swimmers (a power-up beyond a bridge is collected by the power-up task: `AI15.13`). (4) The tests first played the plan alone (`StandardBot(plan)`: no style), the arena plays the registry's bot (`StandardBot(level, style)`): the tests of whole matches now do what the arena does, seed for seed.

**Identical where nothing lies beyond water.** `bot_arena` of the base build (v0.5.0) and of this one: TINY, MEDIUM, GAUNTLET and TREASURE, standard bots with seeds 1 to 12 and worker bots with seeds 1 to 4, at the three levels, every score, hash and command count equal in all 192 matches; `--write-baselines` writes the pinned table again, byte for byte (the comment about ISLANDS changed).

**Mutants** (`tools/mutate.py`, one fault each, the test that must notice it; the specs are not kept): 20 faults, 17 caught, 3 survived. The first round (18 faults) caught 10 and showed six gaps in the tests, fixed with `AI15.12` (the lab moved to SMALL: ants on another island are never guarded, so the first one proved nothing), `AI15.14`, `AI15.15`, the bound of `AI17.4`, `AI17.9` and `AI17.10`.

| Fault | File | Result |
|---|---|---|
| a pile's limit ignored by the fallback of `HarvestTask` / 100 ants to a pile over a bridge | `tasks.cpp`, `island_tasks.cpp` | caught: AI15.6 (both) |
| no pile closed / the way over a retired bridge not counted / no renewal | `island_tasks.cpp` | caught: AI15.14 / AI15.15 / AI15.9 |
| a pile not closed for a trip longer than the life margin of a bridge that is not retired yet | `island_tasks.cpp` | **survived**: for every pile of the labs the retire rule closes first; it differs only for a trip over 250 ticks behind a bridge near the hill (the ring over two bridges at Hard) |
| an ant on a bridge sent off only when it stands | `island_tasks.cpp` | **survived**: on a bridge of three tiles an order cannot reach a walking ant before it has crossed; a longer bridge is needed |
| an idle ant by a hot end stays / the guard repeats an order at every look / the ends counted over water | `island_tasks.cpp` | caught: AI15.10 / AI15.11 / AI15.12 |
| a Swimmer digs with no worker at the hill | `island_tasks.cpp` | caught: AI17.6 |
| the next hop ordered while an ant stands by the landing / a diagonal tile for the planter / a hop with too few hit points / a dud seen only by the timeout | `island_expedition.cpp` | caught: AI17.10 / AI17.1 / AI17.2 / AI17.4 |
| a second hop ordered while one is in flight | `island_expedition.cpp` | **survived**: equivalent, the next ant waits off S while one is in flight, so none stands on it |
| the row's first steps not kept free (the three places together) | `island_expedition.cpp` | caught: AI17.9 |
| any number of Swimmers to a pile / the piles no walker reaches not first / the walker's step weights in the ferry's search | `island_ferry.cpp` | caught: AI18.6 / AI19.4 / AI18.4 |

**Sanitizers.** AddressSanitizer and UBSan together (`-DENABLE_ASAN=ON -DANTS_WERROR=OFF`; that build also prints a `-Wsign-conversion` warning of `include/ants_sim/match_stats.hpp:159`, which is not part of this work). On a release build (`-DCMAKE_BUILD_TYPE=Release`, 16 minutes for `test_ai` on a loaded machine): `test_ai` (194 cases, 567,238 assertions), the island matches (suite 2.28: 3 cases, 109 assertions) and the worker suite (23 cases, 4,427 assertions, none left out): no failure and no report. Without optimization (the build of `./run_tests.sh --asan`): a whole run of `test_ai` reached its last two cases with no report and one failure that is not the sanitizers': `AI13.1` bounds a whole TREASURE match of four Hard bots to 60 s and it took 72 s there on a loaded machine (the release build passes it); the island cases (AI14, AI15, AI17, AI18 and AI19: 45 cases) and the island matches had run in full before, with no report.

**Found on the way** (each fixed, each with a test): a crew ant that waited next to the end of a row shut the Swimmer in for good (AI17.9); a bridge that collapsed under an ant because the guard repeated orders until the budget of the level was spent and the one that mattered left 29 ticks late, and because ends were counted across the lake (AI15.10 - AI15.12); two of five ants thrown into the water by a landing on an occupied tile (AI17.3); an expedition counted one Swimmer too few at its end.

## Open (for the coordinator)

- **The engine's comment** at `src/ants_sim/combat_system.cpp` `remove_ant` ("hatches one for free") is imprecise in the same way as the document was: the egg is not refused below 200 points, but its price `min(200, max(0, score))` is taken. Behaviour is right; the comment is not changed here because B4-1 touches nothing in `src/ants_sim`.
- **Showing the style** (proposed, not built): on the results screen after the match ("Bot (Hard) - raider"), never in the lobby or on the wire.
- **B4a** (island hops, the swimmer ferry): not built in B4-1, built since (`docs/BOTS.md`, "Islands"). **B5** (self-play tuning, the community-map sweep): not done; the plans' numbers were tuned on the five maps with food on foot, TREASURE first. There was no library of community maps on the development machine, so the sweep of the maps that a server hosts was not run with this milestone (AI13.3, sixteen random worlds with lakes, rocks, unreachable piles and power-ups anywhere: nothing refused, bit-reproducible, and the ASan and UBSan run of the whole suite stood in); the independent review ran it afterwards, and "State at handoff" above has its findings and the numbers after the fixes.
- **Weaknesses that are written down, not fixed:** the Medium aggressive style is the weakest of the Medium styles against equals (19.3 against 25.3 to 28.5 percent in the round robin), the Hard aggressive style wins 16.0 percent against three plain Hard bots (30.2 for the raider) and loses the cross table 40 to 60, Hard's two-ant contest costs about 5 percent of the duel margin on TREASURE, the harvesting Combat Ant costs eleven points of win rate in the Hard mirror, Easy keeps 2.5 points less than the worker against a Hard aggressor on TINY and SMALL, Medium ties with the worker on TINY and SMALL, a bot never follows a thief into the ally's base and never invites.
- **Adaptive tactics** (the coordinator's wish to switch the conflict tactics on once the opponents fight) are partly built: the second Combat Ant (Hard) and the aggressive squad's learning react to blows; the opening, the walls and the raids are fixed per level and style. A full adaptive switch was not measured to pay (the conflict tactics that were switched on lost in symmetric play).

## Tuning keys of `bot_arena` (`--tune K=V,...`, or `--seat N=standard+K=V,K=V:LEVEL[:STYLE]` for one seat)

The keys are those of `apply_tune` in `tools/bot_arena.cpp`; every one sets a field of `LevelPlan` (`include/ants_ai/tactics.hpp`), a flag with 0 or 1:
`defenders`, `leash`, `linger`, `aid`; the economy and the opening: `contest`, `clow`, `chigh`, `rankrem`, `cone`, `creact`, `copen`, `copenmin`, `typedh`, `firew`, `chv` (the Combat Ant harvests), `gate`, `gatepred`, `gatelat`, `gatestaged`, `gategap`, `gatefails` (the gate's pause after clicks that deliver nothing); the stall detector: `stall`, `repeat`, `repwindow`, `fallback` (0 switches a trigger off); the walls and the counters: `walls` (0 never, 1 thief seen, 2 thief possible, 3 early), `renew`, `counters`, `bhit`; power-ups and raids: `secure`, `securek` (bit mask of ant types), `combat` (Combat Ants wanted), `combatx`, `thief`, `steals`, `raid`, `raidmin`, `raidblack`, `avoid`, `ambush`, `ambushticks`, `ambushpause`, `ambushdist`, `ambushn`; fights: `intercept`, `fiststrict`, `combat_early`, `combat_idle`, `guard`, `strike`, `strikew`, `strikef`, `strikeres`, `strikeodds`, `wipe`, `hatch`, `hatchsq`, `hatchextra`, `allyhelp`; the squad: `harass`, `harassw`, `harassres`, `harasshp`, `harassodds`, `harassstrong`, `harassretreat`, `harasspause`, `harassidlew`, `harassnear`, `harassleader`, `harassidle`, `harassstick`, `harassdist`, `harassfar`, `harassstation`, `harassrange`, `harassrel`; the sabotage: `sabotage`, `fireextra`, `sabkeeper`, `sabscore`, `sabafter`; and the presets `old` (all conflict tactics off, one Thief), `allon` (all of them on: the strict contest order, strikes, hatching for fights, the wipe-out focus, the ally's help, two Thieves), `agg` (the squad, the Combat Ant in the opening, no guard post) and `idle=N` (the bot sleeps for N ticks: a handicap for the tests of recovery). `BOT_DIAG=1` in the environment makes the standard bots print what their tasks did when a match is over (`DIAG label=... attack_cmds=... raids=... walls=...` on stderr).

The can't-go key (`docs/BOTS.md`, "The can't-go loop"): `cg` (0: the bot as it was before the can't-go fixes: it orders what the engine refuses; for the tournaments that ask what the fixes cost or win).

The island keys (`docs/BOTS.md` "Islands"): `islands` (0: the standard bot as it was before the island machinery), `expedition`, `ferry`, `iswim` (Swimmers wanted), `ibuild` (Swimmers that dig; 0 in the level plans), `ibridge` (ants to a bridge), `iguard`, `iferrypile` (Swimmers of the ferry at one pile).

## Draft of the CHANGELOG entry (the coordinator moves `VERSION`, `CHANGELOG.md`, `STATUS.md` and the README's version line)

```
## v0.3.0 - 2026-10-0X - Computer players that fight: the standard bot

**For players:**
- **Computer players gather food, raid and fight back.** At every level a bot answers a blow on one of its ants with one, two or three ants (Easy, Medium, Hard), sends a hit carrier home, keeps three fire walls in front of its thief hole when a thief threatens (they stop every raid), puts out your fire walls and defuses your bombs where they are in its way, and never sends its last ants into a fight. Medium and Hard bots also take the Fire, Bomber and Thief power-ups of their own side at the start, send ants to the contested middle of the map (when the map starts them with six ants or more), keep a Combat Ant that harvests and fights, and raid the leading team's hill with a Thief.
- **Hard bots guide their carriers at the hill's gate by hand** ("guiding for eating": 4 to 23 percent more food alone on every map) and steal an unguarded Thief power-up for a second thief; Hard bots play the aggressive styles.
- **Four styles.** Every bot draws one at the start of a match (aggressive, economic, raider, defensive; Hard only aggressive or raider; Easy keeps its plan) so that bots do not all play alike. An aggressive bot sends Combat Ants after the carriers of the best opponent that are near them and, at Hard, lights fire walls round that opponent's gate with a stolen Fire Ant. `--bot 2:hard:raider` pins one (a server room's specification too: `"bot": "hard:raider"`); a bot is still called "Bot (Hard)".
- **A computer player that gets stuck backs off.** A click that the game refuses is not sent again for 45 seconds, and a bot that has banked nothing for three minutes (a causeway jammed head on, a hill that its carriers cannot reach) plays plain food gathering for a while instead of repeating the same order.
- The line under "Empty seats at START" (the desktop Host panel, the web page and the README) says what the bots do now: "Bots gather food, raid and fight back."

**Rules / network:** The rules, the network protocol (12) and every golden hash did not change: the bots use the commands that a person can click (an attack names an enemy ant that is not on a hill tile; a move onto a power-up tile is a deliberate pick-up by one ant).

**Fixes:**
- `docs/GAME_REVERSE_ENGINEERING.md`: the mud "humping" bullet, the cost of a forced hatch (`min(200, score)`, not free) and the emergence invulnerability were wrong.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/v0.2.0...v0.3.0), [detailed notes](docs/audit/B4_1_notes.md).
```
