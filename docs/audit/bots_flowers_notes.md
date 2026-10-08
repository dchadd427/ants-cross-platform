# Bots: the flower batch (what was built, how it was checked, what was left out)

On SMALL, MEDIUM and GAUNTLET no Fire power-up lies on the ground at the start: a Fire Ant there comes from a flower dropper or from nowhere, and the bots left the droppers to chance. The batch gives them eyes for the flowers, a rule for whose drop it is and what to fetch, two repairs of the pick-up trip and, for Hard, a watcher. This is a deep-tier change (what a bot sees and does, `AGENTS.md` rule 11). The rules as they are, and the headline numbers, are in [`docs/BOTS.md`](../BOTS.md) ("The flowers", and "The flower batch" in "Measurements"); this file holds what is specific to the check.

The simulation, the lock-step rules, the network protocol and every golden hash are unchanged. The batch has no release number of its own.

## What changed

| Item | Where |
|---|---|
| The view lists the flowers that drop: the plant, the drop tile and the droplet's kind and age as the screen draws them | `BotView::flowers()`, `flower_at()`, `FlowerView` (`bot_view.hpp`, `.cpp`) |
| The analysis lists them with the walking cost of every hill to the drop tile | `MapInfo::flowers()`, `flower_at()`, `FlowerInfo` (`map_info.hpp`, `.cpp`) |
| Whose drop it is: the nearest team, strictly | `drop_side`, `nearest_team` (`tactics.cpp`) |
| What the bot has seen of a flower: the cycle, the next landing, the chance of a kind, counted from the droplets it has seen | `Memory::flower`, `FlowerLog` (`tactics.hpp`) |
| The wants: the kinds of the own side's drop, the Fire at home | `standard_bot.cpp` |
| The recall and the recovery of a pick-up trip; the watcher and its step off the drop tile | `PowerUpTask` (`standard_tasks.hpp`, `.cpp`) |
| Plan values: Hard's five rules and the watcher's two numbers | `LevelPlan::flower_*` (`tactics.hpp`, `.cpp`) |
| What the arena counts of them (`FlowerTally`; the report's fields: `docs/BOTS.md`, "Running bots"); the tune keys `flowers`, `fside`, `ffire`, `frecover`, `frecall`, `fwatch` | `arena.hpp`, `.cpp`, `tools/bot_arena.cpp` |
| Tests AI23.1 - AI23.31; AI19.2 with the plan as shipped and AI19.2b with the rules on | `tests/test_ai/test_ai_flowers.cpp`, `test_ai_island_small.cpp` |

## The comparison

How it was played, and the numbers a reader needs, are in `docs/BOTS.md` ("The flower batch"). What is kept here:

- **By seeds** (Hard, SMALL, MEDIUM and GAUNTLET; the difference with its standard error): seeds 1 to 32 +14.6 +- 12.8 (576 matches), 33 to 60 +64.7 +- 14.6 (504), 61 to 120 +29.1 +- 9.7 (1,080), 121 to 180 +31.3 +- 9.1 (1,080, the bot as it ships with the gate's wait). The halves differ more than their errors say: the kind that falls is drawn from the match's random numbers, and a Fire or a Thief drop at the right moment decides a MEDIUM match. The three samples that count are the seeds the rules were made on (+38.0 +- 9.7) and the two fresh ones (+29.1 +- 9.7 and +31.3 +- 9.1): together +32.8 +- 5.5 over 3,240 matches, the same sign and about the same size on MEDIUM in all three (+90.3, +85.3 and +79.9).
- **The rules one at a time, by map** (Hard, seeds 1 to 32, 576 matches a row, one rule against the bot with none; GAUNTLET / MEDIUM / SMALL): the sides -6.4 / -14.8 / +3.8 (all -5.8 +- 11.7); the Fire Ant at home -0.5 / +7.6 / +13.9 (+7.0 +- 12.0); the recovery 0.0 / -0.8 / -1.3; the recall -0.3 / -0.4 / 0.0; the watcher with the sides -8.6 / +54.0 / +4.6 (+16.7 +- 13.0); all five on the same seeds -6.5 / +44.5 / +5.9 (+14.6 +- 12.8).
- **What the bots take** (Hard, seeds 1 to 60, per seat and match, with the rules / without): on MEDIUM Fire 0.61 / 0.51 and Thief 0.65 / 0.54 power-ups, on SMALL Fire 1.30 / 0.80 and Bomber 0.26 / 0.02, on GAUNTLET Fire 0.78 / 0.66 (those taken at a flower: the table in `docs/BOTS.md`).
- **Identity.** The sets of matches in which the rules cannot act, and play the same to the state hash, are in `docs/BOTS.md` ("The flower batch"). Added here: the Hard bot before and after the last edits of the batch (216 matches), and the build after the second review against the build before it (24 whole matches of four Hard bots on the six maps, seeds 1 to 4, and 72 of a Medium, an Easy and two Hard bots in every arrangement on SMALL, MEDIUM and GAUNTLET, seeds 5 and 6: the reports are byte for byte the same, the arena's flower counts included).

**Checks.** The bots' suite, the island suite, the worker suite and the arena's self-test pass in the normal build, under AddressSanitizer and UBSan (GCC 13; the worker suite without its two tables of whole matches, AI3.9 and AI3.12) and with clang 18 (libc++, Debug, `-Werror`), and `./run_tests.sh --fast` passes; the other compilers are CI's.

## The mutants

104 faults of the new code, one at a time (the unmutated tree was built and tested before the first fault, every 10 faults and after the last: all passed): 101 caught, one that means the same as the code and two defensive guards that no test reaches. The table has the first 92, the six added after the first review (N7 below) among them; the 12 added after the second review (N1 and N2 below) are described after it.

| Part | Faults | Caught by |
|---|---|---|
| The view and the map: the droplet's kind and age, the drop tile, the walking costs, the copy of a view | 11 | AI23.1 (4), AI23.2 (2), AI23.2b (2), AI23.3 (2), AI23.16 |
| Whose drop it is, what is wanted, the side of a trip | 10 | AI23.12 (2), AI23.3 (2), AI23.4 (2), AI23.5, AI23.23, AI7.24, AI7.17 |
| The plans: Hard's five rules, none at Medium and Easy | 8 | AI23.7 (5), AI23.4, AI23.5, AI23.16 |
| The recovery of a trip | 6 | AI23.15 (3), AI23.4 (2), AI19.2b |
| The recall | 4 | AI23.9 (4) |
| The log of a flower: the count of droplets, the cycle, the next landing, the chance of a kind | 13 | AI23.8 (10), AI23.16 (2), AI23.11 |
| The watcher and its step off the drop tile | 35 | AI23.21 (6), AI23.10 (4), AI23.19 (4), AI23.24 (4), AI23.16 (3), AI23.17 (2), AI23.5 (2), AI23.28 (2), AI23.11, AI23.20, AI23.25, AI23.26, AI23.27; three are not caught (below) |
| What the arena counts | 5 | AI23.14 (3), AI23.4, AI23.4b |

Of the first 86, 81 were caught at the first run. Five survived it and were looked at: the points-only case of AI23.15 proved nothing (the engine's bite had already made the ant a carrier), so it now asserts that the poked ant carries nothing; no test had a pick-up trip under way for the lacking kind when a watcher is considered (AI23.26) or a flower so far from every hill that the trip is too long (AI23.27); the trip's side was run against AI23 alone, and the older AI7.24 catches it; and one is equivalent: the watcher's ant is released at the top of the step and taken again in the check whether it is still the watcher, in the same step and before any other task runs. The four were run again with the tests as they now are, and caught.

Of the six added, four are caught: the expected landing that does not move on after a landing that did not come, and the same with no grace (AI23.28: a fire wall holds a landing back, the watcher goes home at the first look after 60 ticks and not before), a copy of the view that drops the flowers (AI23.1), and a tie between two hills that counts as a win (an older test). Two are not: the two guards of the step off the drop tile, that its entry is dropped after 200 ticks and when the ant is no longer on the tile. They act only when the ant that took the drop stays busy longer than its power-up animation (a fight, a blast) and no match can be staged that reaches that state, so they stay as defensive code without a test.

Of the 12 added after the second review, all are caught: the expected landing moved on once and not by whole cycles (AI23.29); the go-home threshold with a one-way trip, without the 30 ticks and without the lead (AI23.30); the rank clause of the watcher's choice (AI23.31, which let it through at the first run, because a refused take only delays the watcher until the held ant has walked away, so the held ant is now an idle worker beside the place); the self-test of the tune keys with `frecall` and with `fwatch` left out of `flowers`, `fwatch` and `fside` set to another rule's flag and `ffire` unknown; the tally's look every 40 ticks instead of 4 (AI23.4) and no look when the match ends (AI23.4, a match that ends at 317, between two looks).

## The reviews

The first independent review of the batch (2026-10-06) found no breach of fairness and no defect of the simulation, and these things to fix or say; all are done.

| Finding | What was done |
|---|---|
| S1 the recovery ended every pick-up trip whose ant had food, on every map | it applies only to a trip to a flower's drop tile (`flower_at`); a trip to a power-up of the start goes on as before (AI23.15 plays both) |
| S2 the Fire want held for a drop that no hill walks to (ISLANDS) | the want needs a hill that reaches the drop (AI23.23) |
| S3 plan labels in public text | the rules are named in code, tests and documents |
| S4 comments of more than two lines | two lines each, the facts are in `docs/BOTS.md` |
| S5 test hazards (`front()` of an empty list, an unbounded loop) | asserted first, bounded |
| N1 the droplet's "16 ticks" holds for the first drops only | the poll phase moves a drop 5 ms against the 50 ms tick, so from about the fifth drop four droplets in ten show 17 frames and `lands` is a tick early: harmless (the tolerances are 60 ticks), the comments say so |
| N2 a trip takes the side from `power_up_side`, so a drop is nobody's in it | kept and documented ("The flowers"): the wants decide what is fetched, not the trip |
| N3 the watcher kept its claim when its place had no walk | `give_up()` first (AI23.25) |
| N4 a watcher is of rank 3 and a fight never takes it | a hit ends the watch and the flower is left for 600 ticks (AI23.24) |
| N5 at Easy and Medium the cycle can be a multiple of the real one; the recall is an order that must win a race | only Hard's watcher reads the cycle; both stated in "The flowers" |
| N6 `flowers=1` is not the shipped plan; `fside=0` switches the watcher off | stated in "The flowers" |
| N7 unpinned: a landing that does not come, a kind on its way counted as owned, a one-landing log, the copy of a view, the two guards of the step off | pinned: the wait for a landing that does not come (AI23.28), a trip under way counts as a kind the plan has (AI23.26), a log of one landing (AI23.8), a copy of the view keeps the flowers (AI23.1); the two guards of the step off stay without a test (see "The mutants"); the whole-match identity runs on TINY, TREASURE and ISLANDS above would have caught S1 |
| N8 the arena's self-test plays TINY, so every flower value is 0 | the self-test plays one SMALL match |

A second independent review of the final state (2026-10-08) found no breach of fairness, no defect of the simulation and nothing to fix before the merge, and seven notes and eight document slips; all are done or, where said, a limit:

| Finding | What was done |
|---|---|
| N1 faults the suite let through: a landing held back for several cycles (`if` for `while`), the go-home threshold (a one-way trip, without the 30 ticks, without the lead), the rank clause of the watcher's choice | AI23.29, AI23.30 and AI23.31; the faults are in "The mutants" |
| N2 the six tune keys have no self-test | `bot_arena --selftest` checks that `flowers` switches the five rules together and that each key switches its own |
| N3 a watcher that dies unseen leaves no hold on the flower | a limit (`docs/BOTS.md`, "Known limits"): it takes a blast or a burst between two looks, and a hold after every loss would also hold after losses that say nothing of an enemy |
| N4 a kind that was not among the first three landings has chance 0 until it is seen | a limit (same place): a prior that outlives the third landing would change the play that was measured, and whether it pays is a measurement |
| N5 the arena's tally built the world state at every tick (about 7 percent of an Easy match) | it looks every 4th tick and once when the match ends (a droplet falls for 16 ticks and a pick-up animation lasts longer than 4); the reports of the 96 whole matches of "Identity" above are byte for byte the same before and after, the flower counts included; AI23.4 pins the end |
| N6 two flowers a tile apart could make the watcher's place the other's drop tile; some fields of the view are read by tests only | a limit (same place): no shipped map has such flowers and a guard that no match reaches could not be tested; the fields stay, they are the seams of the tests and the reports |
| N7 the list of accepted deviations was short | the two lines below |
| D1 - D8 document slips | the fairness sentence says what the bot reads; the table of "The flowers" and this note repeat less of other documents; "Medium takes no flower drop" where Medium has no power-up to walk to; the list of tests; the count of faults; the claim for the watcher is kept to MEDIUM; one wording; the fill-ins are committed |

## Accepted deviations and limits

- The drop tile and the walking costs of the hills to it come from the analysis of the start, as the side of a power-up of the start does: a person judges the distance by eye. The droplet's interval and the draw of its kind are never read.
- The view lists the droppers from tick 0, before any droplet has fallen, and leaves out the daisy of TREASURE that does not drop: a person sees the daisies and learns which of them drop by the first droplet. A droplet's age is in ticks, where the screen draws nine frames; what uses it tolerates 60 ticks.
- The cycle is counted from droplets the bot has seen, so at Easy's look (every 100 ticks) and Medium's (every 20) it can read a multiple of the real one; only Hard's watcher reads it.
- The recall is an order and loses the race for an ant within about two tiles of the power-up (not pinned by a test).
- A watcher is a worker that does not harvest while it waits; Hard pays it only while a kind is lacking. No bot guards its drop against a thief.
- The flowers of ISLANDS are not served and Combat and Swimmer drops get no rule of their own.
