# Bots: the can't-go batch (the evidence)

What the batch does, the decisions, the numbers and what is left are in `docs/BOTS.md`, "The can't-go loop". This note keeps the evidence behind them: the study, the checks and the mutants.

## The study

A report after play: "Can't go there." was heard a lot (the bots kept trying to do illegal moves), and a lot on the small map. A measurement of 84 matches of the v0.5.0 bots (a scratch tool on the built libraries, no change to the code) found:

- the bots' own orders are rarely refused: 546 of 28,016 group orders (19.5 in 1,000); in 530 of the 531 refused walks the map analysis of the start had said "reachable", and what refused them came later (a fire ring 312 times, standing ants 206);
- 88.5 percent of the 5,660 reactions are the engine's own order, 79 percent the original's can't-go loop (the mechanism is in `docs/BOTS.md`); no order of a bot ends it (a Stop or a move out at every tick for 300 ticks changed nothing);
- the loops come from thieves shut in on the raid tile of the hole they raided (TREASURE: Hard 35.8 reactions per 1,000 ticks, Medium 25.8) and from the ring that an Aggressive Hard bot's sabotage lights round the best opponent's gate (SMALL: Hard 20.9);
- of the bots' own refused orders (9.6 percent of the reactions): the gate's clicks into a hill that a ring had sealed 328, thief raids into a hole that ants or walls shut 111, rescue and aid clicks 40, attacks 32, abilities 18, and 17 that the attribution (by priority, not by a hook) leaves open.

## Checks

- **The switch.** `cg=0` plays the same 504 matches as the bot before the batch, to the state hash: every score, tick count and command count equal (the four registry bots of one level on TINY, MEDIUM, GAUNTLET and TREASURE, seeds 1 to 12; two new against two of the v0.5.0 strategy, the six arrangements of the seats, seeds 1 to 4; SMALL and ISLANDS, seeds 1 to 12; Easy, Medium and Hard).
- **The suites** (GCC 13, Release, `-Werror`): the bots' suite, the island suite, the worker suite (its pinned tables untouched: the worker bot is the frozen yardstick and has no can't-go check) and the arena's self-test pass; so does `./run_tests.sh --fast`.
- **AddressSanitizer and UBSan** (GCC 13, `-fsanitize=address,undefined`, halt on error, leaks off): the same suites, the worker suite without its two pinned tables AI3.9 and AI3.12 (they take too long under the sanitizer): no report.
- **Another compiler** (clang 18, libc++, Debug, `-Werror`): the same suites build without a warning and pass.
- **The cost of the counter.** It looks at the ants only on a tick that posted a can't-go news item or while an ant still shows the state (the engine puts an ant in it only together with that news item), so a match without the loop pays nothing for it (AI22.11 holds the counts equal to a look at every tick).

## The mutants

84 faults of the new code, one at a time; the unmutated tree was built and tested before the first fault, every 25 faults and after the last (all passed). All 84 were caught:

| Fix | Faults | Caught by |
|---|---|---|
| The counter: what is counted (both texts, the window of 10 ticks, began against repeated, orders, refused) | 17 | AI22.1 (4), AI22.2, AI22.3 (3), AI22.4 (2), AI22.5, AI22.7, AI22.9 (5) |
| CG1, the map as it is now: the field, `reaches_hill`, the gate, the rescue, the aid, the Swimmer | 18 | AI1.23 (7), AI10.8 (3), AI10.9 (2), AI10.10, AI10.11, AI10.13 (4) |
| CG2, the raid rule and `raid_min_free` | 20 | AI12.4 (19), the self-test of `bot_arena` (the key `raidfree`) |
| CG5, the ramp | 5 | AI10.12 (5) |
| The switch and its plumbing: `cantgo_aware` handed to the gate and to the economy, the plain economy of the stall detector, the defaults | 11 | AI10.14 (6), AI7.29 (2), AI10.10, AI12.4 (2) |
| The review's additions: the counter's look only on a flagged tick, the wait for an ant on the ramp, the aid's tries, the range of the key `raidfree` | 13 | AI22.11 (6), AI10.12 (4), AI10.11, the self-test of `bot_arena` (2) |

Two of the 84 did not count at the first run and were run again with a corrected spec, and both were caught: one fault could not be built (an unused parameter under `-Werror`), one was run with a test that cannot see it (that the economy is handed the flag is seen by AI10.10, not by AI10.14, which plays the switch off). Three survived the run on the tree of the review's fixes and were caught once a test saw them: a wait on the ramp that is not started again for the next ant that comes to stand there (AI10.12, e), a ramp check that ignores the switch (AI10.14, e: the click comes at the first look), and the default of the switch in the plans (AI12.4). An earlier fault of the raid rule that survived was an equivalent one (a clamp of the minimum to at least 1: a hole with no free tile has no walk into it); the clamp was removed.

## The gate and the ant that leaves

A report after play (2026-10-05): the bot sometimes sends an ant into the hole before the other ant has fully exited, which causes a can't go. What the bot does about it is in `docs/BOTS.md`, "The can't-go loop"; this section keeps the study, the second set of seeds, the rules tried and the checks.

**The study.** The study tool of the first section, with the tiles of the hill added to its trace, replayed 120 matches of four Hard bots (TINY, SMALL, MEDIUM, GAUNTLET and TREASURE, seeds 1 to 24) and counted the gate's clicks onto the entrance that the walk refused because of an ant on the ramp:

- 116 refused clicks, in 57 matches. 94 met an own ant without food that was leaving over the ramp (it stops behind another ant of the gate that walks in, and the view draws it as Walking, so the ramp check of the can't-go batch, which looks for an ant that does not walk, does not see it); 22 met an own ant that stood idle on the ramp (19 of them in one match, SMALL seed 1).
- A click is decided up to 13 ticks before the engine gets it (the plan's latency and the predictive click), so a look at the ramp cannot be perfect.

**What ships.** The click waits, 60 ticks at the most, while an own ant without food is on the ramp (`LevelPlan::gate_leaver_ticks`, Hard's; `cg=0` and `gatehold=0` switch it off). On all 240 matches (seeds 1 to 48, below) the clicks that meet a leaving ant fall from 170, in 113 matches, to 140, in 100: 0.708 to 0.583 a match, a difference of -0.125 +- 0.069 a match, paired by match (map and seed): about a sixth, small and not sure. On the first 120 matches, on which the rule was chosen among seven, it looked like half: 116 refused clicks without the wait, 62 with it, in 46 matches, none for an idle ant; they were decided before the leaver stepped onto the ramp, or while it was held up at the hill, where the view does not show it. The control (the same code with the wait off, in the tool) gives the 116 again, event for event.

**Seeds played afterwards.** The rule was chosen on those 120 matches, so the study was repeated on seeds 25 to 48 (120 matches, the same code with the wait off and on). The kind that meets a leaving ant: 76 without the wait, 78 with it (56 and 54 matches), a difference of +0.017 +- 0.083 a match (-0.267 +- 0.110 on the first set). The kind that meets an idle ant: 4 without the wait, 80 with it, 77 of them in one match (SMALL, seed 39), the jam below.

**The ramp that stays shut.** In SMALL seed 39 with the wait, an own ant stood on the ramp from tick 3,700 to the end and the gate's clicks were refused every 20 to 40 ticks (244 orders against 74 without the wait; the seat banked 750 points against 825). The trace of the gate's looks shows how it began: at tick 3,672 the click for the next carrier was decided while the depositor still stood on the entrance (so the wait was not involved); at 3,676 the depositor stepped onto the ramp and the carrier came to the tile beside it; the two blocked each other, and the bot's own carriers ended up parked on the queue-row tiles that the depositor would step to. The first set has one more ant that stays on the ramp, in SMALL seed 1 without the wait (19 clicks; an idle Swimmer on the ramp from tick 5,836), with another cause: a ring of the enemy's fire walls stood round the gate's doorstep from tick 5,622 to at least tick 8,424 and had shut the Swimmer in on the ramp, and at ticks 8,214 to 8,424 the queue row is empty while the Swimmer still stands there idle. The wait neither causes nor cures it: seat-matches with 40 or more refused orders (a stuck gate, though not every one need be this jam) on SMALL, seeds 49 to 120, 72 matches of four Hard bots: 2 with the wait (seeds 74 and 87), 2 without (seeds 52 and 69); with 20 or more, 4 and 4 (a sample that could not show a small difference). A rule for the ant that stands on the ramp for good is not built; each cause needs one of its own (ants that meet head on in a one-wide way; an ant left idle on the ramp after a ring of fire).

**Rules tried** (refused clicks in the first 120 matches; strength: Hard, six maps, seeds 1 to 24, the six arrangements of two bots with the rule against two without it, 864 matches, the score difference per match and bot with its standard error; all on the seeds that the choice was made on):

| Rule | Refused clicks | Strength |
|---|---|---|
| none (the bot as it was) | 116 | |
| **wait 60 ticks (ships)** | 62 | +3.3 +- 10.1 |
| no carrier is sent to the queue row while a clip runs or an ant still leaves | 77 | +0.2 +- 9.7 |
| that and the wait | 47 | -13.1 +- 10.1 |
| only a standing carrier is chosen while an ant is about | 53 | -1.9 +- 9.7 |
| the last two | 51 | -20.5 +- 10.1 |
| all three | 34 | -23.8 +- 9.8 |

A gate that holds the carriers back more refuses fewer clicks and banks less: each of the two stricter rules alone costs nothing that the measurement can see, the combinations cost 13 to 24 points a match and bot (1.3 to 2.4 standard errors). The wait alone is the one whose cost the measurement cannot see either way (a standard error is 10 points of about 1,200). The arena's counter of refused orders, a count by timing that takes in every order, reads 1.19 against 1.21 a seat and match for the wait (864 matches), within its noise: the exact count above is the evidence for the click, the arena's for "no harm".

**The mutants.** 11 faults of the wait, one at a time (the unmutated tree was built and tested before the first and after the last): the click looking at the entrance instead of the ramp, counting ants with food, ignoring the can't-go switch, the wait never starting again, starting again at every look, the limit inclusive, no limit, the wait not applied, not taken from the plan, Hard's plan without it, and the whole step stopping while the click waits (the carrier is then not brought to the queue row at the first look either). All 11 were caught, by AI10.15 (ten) and the plan test AI10.2 (one); the three baselines of the unmutated tree (before the first fault, after the tenth and after the last) passed.

Not pinned by any test: the wait also holds a click that is decided early (the predictive click, a look's latency before the gate is free); a fault that lets that kind through survives every test. It changes no match that was played: in the independent review's 36 matches of four Hard bots none of the 1,457 looks at which the click was held was an early one.

**Checks.** `cg=0` plays the same 72 matches, to the state hash, as `cg=0` of the bot before the change (four bots of one level, Hard and Medium, the six maps, seeds 1 to 6), and Easy and Medium as they ship (they have no wait) play the same 72 matches as the bot before the change. One old test, AI15.11, counted on the match as the old gate played it (stops that repeat in a whole match of four Hard bots on SMALL); it now plays that match with the wait off, so its premise holds as before. The bots' suite, the island suite, the worker suite (without its two pinned tables under the sanitizer, as above) and the arena's self-test pass under AddressSanitizer and UBSan (GCC 13; AI10 and AI15.11, the tests that changed last, were run again alone on the final build) and with clang 18 (libc++, Debug, `-Werror`), and `./run_tests.sh --fast` passes; the other compilers are CI's.
