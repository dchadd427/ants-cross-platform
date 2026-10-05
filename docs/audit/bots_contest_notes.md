# Bots: the contest batch (what was built, how it was checked, what was left out)

The owner's reports of 2026-10-05 (the hard bots do not fight unless they defend; the losing player should be a little more aggressive; the food order; health and the kill; three bugs of the fire play) and the plan that answered them (stage 1: the food order, the bot's own attacks, the escalation of a bot that is behind, the endgame, health-aware fights, the kill, the fire play). This is a deep-tier change (what a bot sees and does, `AGENTS.md` rule 11). The rules as they are, and the numbers, are in [`docs/BOTS.md`](../BOTS.md) (the sections "The race for contested food", "Fights of its own", "Fire play", "Behind the leader and the endgame" and, in "Measurements", "The contest batch"); this file holds what is specific to the check.

The simulation, the lock-step rules, the network protocol and every golden hash are unchanged. Code and docs call this batch "the contest batch": it has no release number of its own.

## What changed

| Item | Where |
|---|---|
| The view lists the hit points (1 to 10) of every ant, the other teams' too: the owner's decision on what players know | `bot_view.cpp`, `docs/BOTS.md` ("Fairness in detail") |
| The race: the ants that the gate cannot use go to the contested piles first | `HarvestTask` (`tasks.cpp`), `LevelPlan::race*` |
| Fights: health-aware, the engine's kill plan, hunts of wounded enemies (two blows), the skirmish (a flag, off) | `FightTask` (`standard_tasks.cpp`), `kill_plan` |
| Fire: the safe fire-in (a Fire Ant to light, escorts only then, give-up), the defence against being fired in | `SabotageTask`, `WallTask`, `FightTask` |
| No special order onto an ant (a dying one too): the controller refuses, the tasks step aside; the simulation is not changed | `BotController::allowed`, the tasks |
| Behind the leader (the pressure and its tiers; workers never join) and the last minute (the leader guards) | `standing_of` (`tactics.cpp`), `StrikeTask`, `RaidTask`, `HarassTask` |
| Tests AI20.1 - AI20.9, AI1.1b, AI2.22 | `tests/test_ai/test_ai_race.cpp`, `test_ai_contest.cpp`, `contest_helpers.hpp`, `test_ai_view.cpp`, `test_ai_controller.cpp` |
| Tuning keys of `bot_arena` for every rule (`race`, `racefloor`, `raceslack`, `hunt`, `huntblows`, `huntodds`, `health`, `skirm`, `sabsafe`, `sabescort`, `firedef`, `catchup`, `cu1` - `cu3`, `cuworkers`, `culift`, `cuwants`, `endgame`, `styled` and more); `prev=1` turns the plan rules of the contest batch off | `tools/bot_arena.cpp` |

## The comparison

One bot with the rules of the contest batch against three of the strategy of v0.5.0 (`prev=1`), at every level on every map, as in B4-1: win rate, margin, kills and losses. `prev=1` is not byte for byte the binary of v0.5.0 (the controller's refusal, the view's hit points and `attackable()`'s refusal of an enemy that stands on a fire wall stay): a mirror of four Hard bots on TREASURE holds the two to the same within noise: the binary of v0.5.0 (48 matches) 2,200 points, 2,343 banked, 143 raided, 0.67 kills a match, `prev=1` (24 matches) 2,201, 2,344, 142 and 0.66 (`docs/BOTS.md`, "The contest batch"). Against the real v0.5.0 binary the commands first differ at tick 216 (a thief's raid names another tile of the enemy mound when an ant stands on the usual one) and the state hashes from about tick 4,100.

## Mutants

Spec files kept outside the repository, `tools/mutate.py`, one change at a time, each with the test that should notice it (47 mutants, 44 caught, 3 equivalent). A first round left nine survivors and five mutants that did not compile; they became tests (the hunts that begin and end at once, a fresh enemy in the scope of a long plan, the more wounded of two targets, the extra defender of the guard stance, Easy's Thief) and the controller's treatment of a dying ant, then the round was repeated. A review of the safe fire-in found that the escorts were called to a gate before a Fire Ant existed to light anything, and were taken again after every release; that is fixed, and the last two mutants hold it.

| Mutant | File | Test | Result |
|---|---|---|---|
| `race-two-competitors-hopeless` | `tasks.cpp` | AI20.1 | caught |
| `race-army-never-holds` | `tasks.cpp` | AI20.1 | caught |
| `race-without-min-ants` | `tasks.cpp` | AI20.1 | caught |
| `race-without-window` | `tasks.cpp` | AI20.1 | caught |
| `race-gate-ignored` | `tasks.cpp` | AI20.1 | caught |
| `race-never-surplus` | `tasks.cpp` | AI20.1 | caught |
| `view-hp-hidden-for-others` | `bot_view.cpp` | AI1.1 | caught |
| `controller-special-at-occupied` | `bot_controller.cpp` | AI2.22 | caught |
| `hunt-fresh-allowed` | `standard_tasks.cpp` | AI20.4 | caught |
| `hunt-odds-ignored` | `standard_tasks.cpp` | AI20.5 | caught |
| `hunt-scope-everywhere` | `standard_tasks.cpp` | AI20.5 | caught |
| `hunt-reserve-ignored` | `standard_tasks.cpp` | AI20.5 | caught |
| `hunt-carriers-taken` | `standard_tasks.cpp` | AI20.5 | caught |
| `hunt-wounded-not-first` | `standard_tasks.cpp` | AI20.5 | caught |
| `kill-plan-parity-swapped` | `standard_tasks.cpp` | AI20.5 | caught |
| `kill-plan-odd-without-opener` | `standard_tasks.cpp` | AI20.5 | caught |
| `strikers-parity-off` | `standard_tasks.cpp` | AI20.4 | caught |
| `fight-waits-always` | `standard_tasks.cpp` | AI20.3 | caught |
| `fight-never-waits` | `standard_tasks.cpp` | AI20.5 | caught |
| `escape-off` | `standard_tasks.cpp` | AI20.3 | caught |
| `keep-one-blow-from-death-off` | `standard_tasks.cpp` | AI20.3 | caught |
| `relief-off` | `standard_tasks.cpp` | AI20.3 | caught |
| `fire-defence-off` | `standard_tasks.cpp` | AI20.7 | caught |
| `fired-in-wall-never` | `standard_tasks.cpp` | AI20.7 | caught |
| `sabotage-lone-fire-ant` | `standard_tasks.cpp` | AI20.6 | caught |
| `sabotage-no-escort-needed` | `standard_tasks.cpp` | AI20.6 | caught |
| `sabotage-no-giveup` | `standard_tasks.cpp` | AI20.6 | caught |
| `raid-occupied-entrance` | `standard_tasks.cpp` | AI20.8 | caught |
| `sabotage-occupied-ring` | `standard_tasks.cpp` | AI20.8 | equivalent |
| `wall-build-occupied` | `standard_tasks.cpp` | AI20.8 | equivalent |
| `pressure-ignores-time` | `tactics.cpp` | AI20.9 | caught |
| `tiers-same-for-all-levels` | `tactics.cpp` | AI20.9 | caught |
| `guard-never` | `tactics.cpp` | AI20.9 | caught |
| `endgame-no-allin` | `tactics.cpp` | AI20.9 | caught |
| `workers-join-at-tier1` | `standard_tasks.cpp` | AI20.9 | caught |
| `easy-raids-never` | `standard_bot.cpp` | AI20.9 | caught |
| `offence-in-guard-stance` | `standard_tasks.cpp` | AI20.9 | caught |
| `strike-in-guard-stance` | `standard_tasks.cpp` | AI20.9 | equivalent |
| `guard-defender-bonus` | `standard_tasks.cpp` | AI20.9 | caught |
| `workers-join-by-default` | `tactics.hpp` | AI20.9 | caught |
| `hunt-blows-default-three` | `tactics.hpp` | AI20.5 | caught |
| `lift-tier-never` | `standard_bot.cpp` | AI20.9 | caught |
| `controller-special-dying-ant-passes` | `bot_controller.cpp` | AI2.22 | caught |
| `controller-special-own-ant-passes` | `bot_controller.cpp` | AI2.22 | caught |
| `controller-special-ally-passes` | `bot_controller.cpp` | AI2.22 | caught |
| `escorts-without-fire-ant` | `standard_tasks.cpp` | AI20.6 | caught |
| `escorts-never-let-go` | `standard_tasks.cpp` | AI20.6 | caught |

Equivalent: the occupant checks that `SabotageTask` and `WallTask` make before an ignite order (the engine's predicted acknowledgement refuses it already; the controller refuses it too) and the guard stance of `StrikeTask` (a leader has tier 0 and never strikes, unless `wipe_focus`, which is off in every plan).

## Sanitizers

GCC 13, `-fsanitize=address,undefined`, a Release build with `-DANTS_WERROR=OFF` (GCC 13 gives known `-Wsign-conversion` false positives under UBSan in older files; the ordinary build with `-Werror` is clean, and so is a syntax pass of every changed file with clang 18 at the project's warning level). On the code of the last commit of the batch: the whole suite 2.20 `test_ai`: 160 cases, 526,044 assertions, no failure and no sanitizer report; the worker suite without its two pinned tables (AI3.9, AI3.12, as `run_tests.sh --asan` leaves them out): 21 cases, 4,218 assertions, none failed; `bot_arena --selftest`: 140 checks, no failure.

## Tests whose premise changed

| Test | Change | Why |
|---|---|---|
| AI1.1, AI1.25, AI2.5, AI2.16, AI2.21 | the hit points of another team's ant are the engine's (they were 0) | the owner's decision |
| AI8.2, AI8.4, AI8.5, AI11.8, AI11.9 | the plan sets `race = false` | they hold the opening of v0.5.0 (one or two ants to the middle), which stays selectable; the race has AI20.1 |
| AI7.15 | `plan.catchup = false` in two parts | the world has no food, so any deficit is the highest pressure at once (AI20.9 holds the tiers) |
| the fight plans of AI9.x (`fight_plan()`) | `catchup = false` | they hold the old trigger, a margin in points |

## "Can't go there." reactions

Counted by replaying recorded matches with the locomotion trace (every ant that shows "Can't go there." or "Can't do that..."), four Hard bots, per 1,000 ticks played: SMALL (seeds 1 to 8) 20.9 with the bots of v0.5.0 and 1.0 with the bots of the contest batch (1,609 reactions and 75); TREASURE (seeds 1 to 4) 35.8 and 28.1 (2,065 and 1,619). The SMALL cut is the rule on special orders and the safe fire-in (the orders at an occupied tile and the lone Fire Ant's walls); the thieves' raid tile on TREASURE, where most of the rest comes from, is untouched. Nothing here opens a ring over an enemy ant: that is the owner's decision.

## Left out, and why

- **The skirmish of the bot's own** (`plan.skirmish`): built, off. Hard won 14.1 percent with it and 28.6 without (TREASURE, 96 matches each).
- **Workers in the catch-up strike** (`catchup_workers_tier`, never) and hunts of three or more blows (`hunt_blows` 2): measured costs, in "The contest batch".
- **A race floor** (`race_floor`: at least this many ants race whatever the gate can use): 2 ants lost 12 points of win rate (22.6 against 34.9 percent) and 0 ships.
- **The strict contest order** (v0.3.0): still off (6.2 percent).
- **Who gets to a dropped Fire power-up first** (a refinement of the safe fire-in on maps with droppers, see "Known limits").
- **A forward guard**: before the escorts waited for a Fire Ant, two Combat Ants parked at the best opponent's entrance and punched what came in and out. It was worth about 8 points of win rate to the Hard aggressive style on TREASURE (26.7 against 18.1 percent, 144 matches) and to Hard on GAUNTLET (32.8 against 25.0, 64 matches), and it was an accident of the rule, not a design; a bot that waits at an enemy's gate with no purpose is the owner's to decide, and it is left to stage 2.
- **Stage 2** (bombers, power-ups, the specials, the flower droppers, the rush of the enemy's Fire power-up before the fire-in): not built; nothing here makes it harder (`can_put_out` of the fire-in is the rule that the rush would satisfy).

## What the owner decided about the calls of this batch (2026-10-05, Pacific morning)

The hand-back of the batch marked four things as the owner's to decide, each with what the batch had built; he approved all four as built.

- **Hunts** stay (two blows; `hunt=0` is the plan without them): half a kill a match more, and 6 points of win rate on Hard GAUNTLET.
- **The safe fire-in** stays as built, with its cost of about 4 points of win rate on SMALL at Hard (the flowers there drop Fire power-ups); `sabsafe=0` is the old fire-in. A fire-in that also works where this one refuses (take or deny the Fire power-ups first) is stage 2.
- **A forward guard** (two Combat Ants parked at the best opponent's entrance) stays out; stage 2 may build one on purpose.
- **MEDIUM at Hard, near 23 percent against three bots of v0.5.0**, is accepted as it is (noise, or no single rule); nothing in the batch moves it.

Nothing in the code changed for these decisions: the bot is what the batch built.
