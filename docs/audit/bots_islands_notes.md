# Bots: the expedition batch (what was built, how it was checked, what was left out)

On ISLANDS no hill can walk to a Swimmer: the twelve lie in rows in the corners, beyond water, and the standard bot flies a crew of plain workers there with bombs (`ExpeditionTask`, `docs/BOTS.md` "Islands"). The owner asked why the bots do not bomb their own Bomber over the water with the crew, and watched them lose ants and time on the way. The batch lets the Bomber fly over with the crew where the way needs it, plans the way by cost, takes the waits out of the hops, and repairs the places where an ant that could not go held the others up. The checks went beyond the quick tier and one review (`AGENTS.md`, "How much checking"): mutants of the new code, sanitizer and clang runs, and three reviews. The rules as they are, and the headline numbers, are in [`docs/BOTS.md`](../BOTS.md) ("The expedition", and "The expedition batch" in "Measurements"); this file holds what is specific to the check.

The simulation, the lock-step rules, the network protocol and every golden hash are unchanged. The batch has no release number of its own.

## What changed

| Item | Where |
|---|---|
| The route is the cheapest way by flights: a flight costs 2 and one more from an island that has no Bomber to take; the hill's island needs a Bomber of its own; Easy keeps the old routes | `ExpeditionTask::plan` (`island_expedition.cpp`), `Params::fly_on`, `LevelPlan::island_fly_on` (`tactics.hpp`, `.cpp`), `StandardBot::expedition_params` |
| The Bomber flies on as the last of the crew, on a bomb of its own: when no crew ant that can hop is left behind and the next island has no Bomber to take, or when the crew that is left is one ant short of the tokens; the leg it leaves is over (`Leg::flown`) and the leg it lands on adopts it | `drive_leg` (the flier block and the adoption), `crew_to_come`, `crew_one_short`, `able_to_take`, `tokens_needed`, `swimmers_more` |
| The Bomber takes the last token that is wanted and no other, and only after every plain ant | `drive_row` |
| Quicker hops: the landing is busy only while an own ant stands on it and does not walk, or an ant of another team is on or beside it; a second order to an ant follows after the level's latency and 20 ticks; a dud is told by the state of the ant that burns on B and by nothing else; an ant with fewer than 4 hit points leaves the shore tile for the ants that can still hop | `drive_leg`, `step` (`gap_`) |
| A crew in which no plain ant can take a token any more is given up after 200 ticks, not 2,400 (two tokens or more are left; at Easy, where no Bomber flies on, one) | `crew_hopeless`, `step`, `Params::hopeless_ticks` |
| What the arena counts and reports of an expedition (report format 5) and the tune key `ifly` | `ExpeditionResult`, `read_expedition` (`arena.hpp`, `.cpp`), `tools/bot_arena.cpp` |
| Tests AI17.14 - AI17.39 and AI14.11 (the engine lets a Bomber step onto its own bomb); AI17.1, AI17.2, AI17.4 and AI17.12 with their numbers moved | `test_ai_island_expedition.cpp`, `test_ai_islands.cpp` |

## The comparison

The head-to-head, the flight alone, the four bots of one level, the reserve Bomber and the matches that stay the same on the five maps without such water are in `docs/BOTS.md` ("The expedition batch"). Added here:

- **Latency.** The arena's command latency is 3 ticks; the expedition was also played at 0 and 30 (four bots of one level, the first 7,200 ticks; seeds 61 to 150 at 3, 61 to 90 at the others): at Medium and Hard the four seats take 2.97 to 3.00 Swimmers a match on average, one or two attempts are given up in 120 to 360 expeditions, and 19 to 20 percent of the bombs are duds (the engine's roll is one in five; the first version's clock overstated it, below).
- **The measured code is the final code.** The tables were measured before the last edit of the production code (the dud branch lost two conditions that the state of the ant makes redundant); the build of the final tree plays the four-bot matches of ISLANDS again (seeds 1 to 120, three levels: 360 matches) to the same state hash, and the arena's reports are equal in every field but Easy's count of duds seen.

**Checks.** The bots' suite, the island suite, the worker suite and the arena's self-test pass in the normal build, under AddressSanitizer and UBSan (GCC 13; the worker suite without its two tables of whole matches, AI3.9 and AI3.12) and with clang 18 (libc++, Debug, `-Werror`), and `./run_tests.sh --fast` passes; the other compilers are CI's.

## The mutants

58 faults of the new code, one at a time (the unmutated tree was built and tested before the first fault, every 10 faults and after the last: all passed): 53 caught and five that nothing in the suite tells from the code. The table names the first test to fail. 56 were made before the reviews; the third review found two more that no test caught (the `Stunned` half of the dud test, and the island of the row in `crew_hopeless`), and AI17.29 and AI17.39 catch them since. The five left, the two just named and the faults of the three tests below were run again on the final tree: the same result.

| Part | Faults | Caught by |
|---|---|---|
| The Bomber flies on: the leg it leaves, the leg that takes it over, when it goes | 15 | AI17.2 (3), AI17.18 (2), AI17.22 (2), AI17.25 (2), AI17.26, AI17.34; 4 not caught (below) |
| The crew that is one ant short, the tokens that are needed, the last token | 15 | AI17.1, AI17.16 (4), AI17.20, AI17.21, AI17.23 (3), AI17.26 (2), AI17.28, AI17.30, AI17.38 |
| The landing and the ants that wait | 7 | AI17.2, AI17.4 (2), AI17.10, AI17.19 (2), AI17.22 |
| The route | 4 | AI17.1 (2), AI17.24; 1 not caught (below) |
| Easy's switch and the plumbing of the plan and of the arena | 4 | AI17.15 (2), AI17.33, the arena's self-test |
| A crew that can go no further | 8 | AI17.1, AI17.23, AI17.31 (2), AI17.35, AI17.36, AI17.37, AI17.39 |
| Duds and the gap between two orders | 5 | AI17.2, AI17.4 (2), AI17.29 (2) |

The first run caught 48 of the 56. Of the eight it let through, three are caught since by tests that play a whole expedition on a seed taken from the arena's own runs: a crew with one token left that is given up though the Bomber is waited for (AI17.36), a Bomber counted as an ant that can take any token, so that the crew is never given up (AI17.37), and a plain ant that does not go before the Bomber at the last token (AI17.38).

Five are left, and each was looked at with a build that counts how often the fault would have changed what the expedition does: four bots of one level on ISLANDS, the first 7,200 ticks of the matches, 1,475 plans in all (Medium and Hard at the arena's latency of 3 ticks, seeds 61 to 150, and at 0 and 30 ticks, seeds 61 to 90; Easy at 3 ticks, seeds 61 to 100).

- The two guards that keep the Bomber on the ground while a crew ant of its own leg, or of the leg before it, is in the air. They decide whether the Bomber joins the crew at one look or at the next: the first stood in its way at 532 looks, the second at 29. In the arena (90 matches a level, seeds 61 to 150) neither fault moves a figure of the expedition: the median tick of the first Swimmer by 24 at most, 2.98 to 3.00 Swimmers a match in every seat, and one attempt given up in the 90 matches, as without the fault. The third review played both in the arena as well (seeds 1 to 40 at the arena's latency): without the first almost every match differs, by a bomb or two (40 of 40 at Medium and at Hard), without the second 2 of 40 at Hard and none at Medium.
- The size of the route in its score, instead of its cost: the row and the entrance chosen are the same in all 1,475 plans. The two scores differ only between routes of one length of which one runs over an island with no Bomber to take, and on ISLANDS that tie is broken the same way by the distance of the landing; a test needs a map where it is not, and no shipped map is one.
- The wish for a Bomber on the next island, with no regard to whether the island has one to take: the same in all 1,475 plans, because the Bomber of every island on the route is made ready while the crew is on its way.
- The mark of a leg whose Bomber has flown on, set whether or not the Bomber is of the crew: the same in all 1,475 plans, because a Bomber flies on only when no crew ant that can hop is left on its leg's island or before it, so a leg that did not mark itself has no hopper and nothing expected, and returns before it orders the Bomber that it still names (AI17.32 plays the hand-over to its end).

## The reviews

Three independent reviews of the batch (2026-10-08) found no breach of fairness, no defect of the simulation and no must-fix. The first read the whole change and played the arena on ISLANDS (1,440 expeditions of four bots of one level, debugger dumps of the task at the end of runs); the second read the tests and the documents against the code and the first mutation run; the third read what was changed after the second and then the whole change again, ran 42 faults of its own through the suites (30 caught; of the 12 it let through, two were real gaps, row C1 and C2, and ten are equivalent or defensive: the five above and five more, below) and played the five maps without such water again (140 matches, the same records as the old build). Everything the three found is done or, where said, a limit.

| Review A: finding | What was done |
|---|---|
| A1 `BOTS.md` says the second order waited 60 ticks at every level | only Hard's gap changed (60 to 44); Medium (80) and Easy (205) were the latency and 20 already, and the text says so |
| A2 a placeholder line in `BOTS.md` | filled |
| A3 a Bomber that had flown on and was stranded (2 hit points, not on the island of the row) made `crew_one_short` false for good, so the next leg's Bomber never flew on and the last token was never taken (1 of 720 Medium expeditions, seed 67 on seat 2 among them) | a Bomber that can still hop or stands on the island of the row covers the last token, a stranded one covers nothing (`able_to_take`; AI17.30) |
| A4 the 60-tick clock called a hop that worked a dud when the ant was still in the air: duds counted at 24 percent at Medium and 28 at Easy against the engine's 20, and the leg's hopper cleared while the ant flew | a dud is told by the state of the ant that burns on B (AI17.29); the counted rate is 19 to 20 percent |
| A5 a crew that cannot finish held 3 or 4 of the seat's 8 ants until the 2,400-tick limit (8 of the 9 expeditions that did not finish) | `crew_hopeless`: given up after 200 ticks (AI17.31 at Medium, AI17.35 at Easy) |
| A6 a route of four flights or more is planned though the one Bomber that flies over islands with no Bomber to take spends 2 hit points on each (and 2 on a dud) | a limit, no shipped map reaches it ("Known limits", expedition batch, 7) |
| A7 a flown-on Bomber taken over by the next leg was still named by the leg it came from | the leg that lost its Bomber clears it and is marked flown (AI17.32) |
| A8 wording: when the flown Bomber plants again, who may hop on a landing, what `crew_size()` counts | the text and the headers say what the code does |

| Review B: finding | What was done |
|---|---|
| B1 clang (the macOS job): an unused lambda capture in AI17.25 | the capture list names what the lambda uses |
| B2 eight changes of the code that no test notices: the four guards of the Bomber's flier block, the plain ant before the Bomber at the last token, the size of the route in its score, the Bomber's wish for a source on the next island, the wiring of `set_params` | the wiring (AI17.33), the hit-point guard (AI17.34) and the plain ant before the Bomber (AI17.38) are pinned; the guard on the island of the Bomber was taken out of the code (the mark `flown` does what it did); the two hopper guards, the route score and the wish for a source stay and are explained in "The mutants" |
| B3 the 60-tick sentence of A1 again | as A1 |
| B4 a test comment with ranges that the final code no longer gives | corrected |
| B5 test titles that claimed more than the tests check (AI17.19, AI17.21), a mode of AI14.11 that proved nothing | the titles say what is checked; AI14.11 tries every flight with a Bomber, the unused mode is gone and the skips are asserted |
| B6 dead code (`order_gap`, a floor that nothing set; a parameter of `strand()`), silent skips in AI17.25 and AI17.26 | removed; the skips are asserted |
| B7 numbers in `BOTS.md` that the final measurements had moved | measured again on the final code and written |
| B8 work-in-progress commit subjects | one commit with a plain subject |

| Review C: finding | What was done |
|---|---|
| C1 the `Stunned` half of the dud test is pinned by no test: an Easy bot looks every 100 ticks and often finds the ant already stunned; without it 38 of 40 four-bot matches differ and the duds counted are 0.14 of the hops, not the engine's 0.20 | AI17.29 plays two more Easy matches (4 and 5 duds); a reading of the burning state alone counts 1 and 3 |
| C2 the clause "stands on the island of the row" of `able_to_take` is pinned for the crew that is one ant short, not inside `crew_hopeless`: a crew of hurt ants that stand on the row island is given up | AI17.39 hurts every plain ant that lands on the row island: four tokens, two Swimmers, nothing given up (without the clause the attempt is given up at tick 4,801 with three tokens and one Swimmer) |
| C3 two sentences of "The mutants" were not true (why the mark-of-the-leg fault is equivalent; that each guard fault changes a few matches) | rewritten |
| C4 AI17.36 and AI17.37 assert the outcome only, and AI17.37's title said 3,897 where the fault gives up at 3,901 | the test records the tick of the last token and the longest stretch without one: AI17.36 asserts a wait of more than 200 ticks (it is 1,500), AI17.37 that the give-up came 200 to 260 ticks after the last token (204) and that one Swimmer was taken |
| C5 a header comment said "no ant of the crew" for no plain ant of the crew, and two tokens left (one at Easy) | corrected |
| C6 `crew_hopeless` is narrower than review A5 asked: one token left while the Bomber cannot come waits the full 2,400 ticks | a limit, already written ("Known limits", expedition batch, 3); not met in 60 seeds of four Easy and four Medium bots |
| C7 a leg's Bomber that a blast throws to another island is taken for one that flew on, and the leg fetches no other | a limit ("Known limits", expedition batch, 9): a fetch gains only where a second Bomber lies on the island, and none was seen |
| C8 an Easy dud whose ant is already idle on B at the look is not counted (5 of 84 duds in matches of one bot) and the leg is freed after 345 ticks | a limit ("Known limits", expedition batch, 8) |
| C9 AI17.25: an assertion inside a lambda leaves the lambda only | the lambda returns, the assertion follows the call |

The ten faults of the third review that are equivalent or defensive: the five of "The mutants" and five more that the arena does not tell from the code in six sets of 200 matches, or only by about 20 ticks in a few: the tile and the "still on this island" test of the dud branch, the shorter of the two patiences of a hopeless crew, the "takes orders" test of the adoption of a Bomber, and the hit-point test alone for a Bomber that covers the last token.

## What was left out

What the batch does not do, and why, is in `docs/BOTS.md` ("Known limits", the expedition batch, 1 to 9).
