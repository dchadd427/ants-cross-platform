# Bots: the can't-go batch (fewer refused orders, a counter, and the loops that stay)

What was built on top of the bots of `docs/audit/bots_integration_notes.md`, how it was checked, what the owner decided and what is left. The section "The can't-go loop" of `docs/BOTS.md` has the mechanism and the numbers; this note has the evidence behind them.

## Why

The owner heard "Can't go there." a lot ("it keeps trying to do illegal moves"; "I was hearing it a lot on the small map"). A measurement of 84 matches of the v0.5.0 bots (scratch tool on the built libraries, no change to the code) found:

- the bots' own orders are rarely refused: 546 of 28,016 group orders (19.5 in 1,000; in 530 of 531 refused walks the map analysis had said reachable);
- 88.5 percent of the 5,660 reactions are the engine's own order, and 79 percent are one mechanism, the original's **can't-go loop**: an ant on a special tile of any hill (queue row, ramp, entrance, raid tile) that is shut in by fire walls and ants shows the clip, and when the clip ends the engine orders it to its own waiting tile; the walk fails in the same tick and the clip starts again, every 8 ticks (worker) or 18 (thief), until a wall burns out or an ant leaves. No order of a bot ends it (a Stop or a move out at every tick for 300 ticks changed nothing);
- the loops come from thieves shut in on the raid tile of the hole they raided (TREASURE: Hard 35.8 reactions per 1,000 ticks, Medium 25.8) and from the ring that an Aggressive Hard bot's sabotage lights round the best opponent's gate (SMALL: Hard 20.9);
- of the bots' own refused orders (9.6 percent of the reactions): the gate's clicks into a hill that a ring had sealed 328, thief raids into a hole that ants or walls shut 111, rescue and aid clicks 40, attacks 32, abilities 18.

## What was built

1. **The counter** (`CantGoTally`, `include/ants_ai/arena.hpp`, `src/ants_ai/arena.cpp`; AI22.1 - AI22.9): `bot_arena` counts per seat and match the can't-go reactions (the engine's news items 58 and 48), the ants that began a can't-go state, the group orders and the orders that were refused (an order followed within 10 ticks by the first reaction of an ant it names). The report's format is 3 (`cantgo`, `cantgo_began`, `orders`, `refused_orders`). The counter changes nothing in a match (AI22.8). AI22.10 pins that the bots' own refused orders stay under 9 in 1,000 on TREASURE at Hard (seeds 1 to 4); the loops are reported, not bounded.
2. **CG1, the map as it is now** (`MapInfo::field_now`, `MapInfo::reaches_hill`; `GateTask`, the economy's rescue in `tasks.cpp` and `CarrierAidTask` in `standard_tasks.cpp`; AI10.8 - AI10.11, AI1.23): a carrier that no walk joins to the hill (a pocket, a ring of fire walls) is sent no order into it, and the gate does not guide while its doorstep is cut off. A Swimmer is never cut off by water. The worker bot, the frozen yardstick, is not changed (`HarvestTask::Params::cantgo_aware` is false by default; AI10.14).
3. **CG2, the raid rule** (`RaidTask::launch`; AI12.3, AI12.4): no raid at a hole while an ant stands on its raid tile, and a tile in front of the hole on which an ant stands still is not a free tile. `LevelPlan::raid_min_free` (1 in every plan, `bot_arena --tune raidfree=N`) is the number of free tiles that a raid needs; see below for why it is not 2.
4. **CG5, the ramp** (`GateTask`; AI10.12): the gate clicks nobody onto the entrance while an own ant stands on the ramp.
5. **The switch** (`LevelPlan::cantgo_aware`, `bot_arena --tune cg=0`): the bot as it was before the batch, match for match.

## The raid rule, and why the quiet variant is not the default

The rule that was planned asked for two free tiles in front of a hole (`raid_min_free` 2). It is quiet and it costs strength where the other side raids at one free tile. Four bots of one level on TREASURE, seeds 1 to 48, the shipped rule and the quiet one each against the bot as it was (`--tune cg=0`), paired by match (mean difference with its standard error over 48 matches):

| | can't-go reactions per 1,000 ticks | refused orders | score |
|---|---|---|---|
| Medium, shipped (`raid_min_free` 1) | 5.70 -> 6.48 (+0.78 +- 0.46) | -1.34 +- 0.22 | +0.2 +- 7.9 |
| Medium, quiet (2) | 5.70 -> 2.27 (-3.42 +- 0.46) | -2.22 +- 0.41 | +15.9 +- 7.3 |
| Hard, shipped | 8.09 -> 8.04 (-0.05 +- 0.53) | -1.87 +- 0.28 | -6.3 +- 5.3 |
| Hard, quiet | 8.09 -> 2.00 (-6.09 +- 0.50) | -4.24 +- 0.28 | +14.7 +- 3.6 |

Mirrored (two bots with the rule and two without, `styled=1` and `styled=1,cg=0`, the six arrangements of the seats; TREASURE; score points a match and bot, with and without):

| | points a match and bot | share of matches won | reactions per 1,000 ticks and seat (with / without the rule) |
|---|---|---|---|
| Medium (144 matches), shipped | +3.6 +- 25.7 | 51 % | 6.02 / 6.16 |
| Medium, quiet | -112.0 +- 27.1 | 36 % | 2.38 / 5.53 |
| Hard (288 matches), shipped | -12.3 +- 20.6 | 47 % | 8.03 / 7.87 |
| Hard, quiet | -37.3 +- 18.9 | 45 % | 2.58 / 7.65 |

The three parts of the planned rule, one at a time (Medium, 144 matches): the two free tiles alone -104.3 +- 33.8, the standing ants on the tiles alone +26.8 +- 27.6, the ant on the raid tile alone +1.4 +- 25.2. So the free tiles are the whole cost, and the two checks that do not cost anything stay. When all four bots have the quiet rule the scores are as high as with the shipped one (a thief that goes to a hole with one free tile is shut in more often than it gets in: raids between equals cost the raider about what they take) but a person raids at one free tile, so against a person the quiet bots would take less than the shipped ones.

## Checks

- **The switch.** `cg=0` of the final binary plays the same 504 matches as the batch's bot, to the state hash: every score, tick count and command count equal (the four registry bots of one level on TINY, MEDIUM, GAUNTLET and TREASURE, seeds 1 to 12; two new against two of the v0.5.0 strategy, the six arrangements of the seats, seeds 1 to 4; SMALL and ISLANDS, seeds 1 to 12; Easy, Medium and Hard: 144 + 288 + 72 matches).
- **Where the matches change** (state hash differs from the batch's bot, of the matches of those sets): Easy 0 of 168; Medium TREASURE 36 of 36, GAUNTLET 5 of 36, the other maps 0; Hard TREASURE 36 of 36, GAUNTLET 21 of 36, MEDIUM 17 of 36, SMALL 3 of 12, the other maps 0.
- **Mutants** (`tools/mutate.py`, one fault at a time, the specs are not kept): 71 faults of the new code, 71 caught; the table is under "The mutants" below.
- **AddressSanitizer and UBSan** (GCC 13, `-fsanitize=address,undefined`, halt on error; leaks off), on the batch before the join with the release branch (tree 4123db6): `test_ai` (228 cases, 568,639 assertions), the island suite (3 cases, 109 assertions), the worker suite without its two pinned tables AI3.9 and AI3.12 (21 cases, 4,218 assertions) and `bot_arena --selftest` (146 checks): no report, every suite passes. The run on the joined tree is still to be made.
- **The suites** (GCC 13, Release, `-Werror`): on the joined tree (the batch with the release branch merged in) suite 2.20 `test_ai` has 235 cases (216 before the batch; 568,979 assertions), the worker suite 2.22 (23 cases, 4,427 assertions, the pinned tables untouched: the worker bot is the frozen yardstick and has no can't-go check), the island suite 2.28 (3 cases, 109 assertions) and `bot_arena --selftest` (146 checks) pass. `./run_tests.sh --fast`: 55 suites, none failed, on the batch before the join and on the release branch after main's v0.8.3 was merged into it; on the joined tree it is still to be run. The arena binary of the final tree plays the same matches as the one that made the tables (32 matches: scores, state hashes, ticks and counts equal).
- **Another compiler** (clang 18, libc++, Debug, `-Werror`), on the batch before the join: the same suites build without a warning and pass: `test_ai` 228 cases (568,630 assertions where the GCC builds count 568,639: nine fewer ran, and every one that ran passed), the island suite 3 cases, the worker suite 23 cases (4,427 assertions), `bot_arena --selftest` 146 checks. The run on the joined tree is still to be made.

### The mutants

71 faults of the new code, one at a time; the unmutated tree was built and tested before the first fault, every 25 faults and after the last (all passed). All 71 were caught:

| Fix | Faults | Caught by |
|---|---|---|
| The counter: what is counted (both texts, the window of 10 ticks, began against repeated, orders, refused) | 17 | AI22.1 (5), AI22.2, AI22.3 (3), AI22.4 (2), AI22.5, AI22.9 (5) |
| CG1, the map as it is now: the field, `reaches_hill`, the gate, the rescue, the aid, the Swimmer | 18 | AI1.23 (7), AI10.8 (3), AI10.9 (2), AI10.10, AI10.11, AI10.13 (4) |
| CG2, the raid rule and `raid_min_free` | 20 | AI12.4 (19), the self-test of `bot_arena` (the key `raidfree`) |
| CG5, the ramp | 5 | AI10.12 (5) |
| The switch and its plumbing: `cantgo_aware` handed to the gate and to the economy, the plain economy of the stall detector, the defaults | 11 | AI10.14 (6), AI7.29 (2), AI10.10, AI12.4, AI22.10 |

Two of the 71 did not count at the first run and were run again with a corrected spec, and both were caught: one fault could not be built (an unused parameter under `-Werror`), one was run with a test that cannot see it (that the economy is handed the flag is seen by AI10.10, not by AI10.14, which plays the switch off). An earlier fault of the raid rule that survived was an equivalent one (a clamp of the minimum to at least 1: a hole with no free tile has no walk into it); the clamp was removed.

## What the owner decided (2026-10-05)

- CG3 (the walls of the thief hole do not close behind an enemy thief) and CG4 (the ring does not close its last tile over an enemy ant) are not built: shutting ants in is the purpose of the thief hole's walls and of the fire-in, and they would weaken the original's tactics.
- Still the owner's: an engine change that stops the re-order at the end of the clip after the n-th failure (it would silence every loop, and it departs from the original); a limit on what a person hears (the can't-go sound for the person's own ants only, as in the original, and optionally at most once in 3 seconds per ant: application only, the simulation and every hash stay as they are); the quiet raid rule (`raid_min_free` 2).

## Left

- **The loops stay (class B).** A person still hears them: thieves of bots that raid a hole with one free tile and are shut in on the raid tile (TREASURE: 6.5 and 8.0 reactions per 1,000 ticks and seat at Medium and Hard), and the enemy ants that a ring or the walls of a hole shut in. The can't-go sound goes to every seat (only the status line is for the own ants), so the person hears every bot's loop. The two cheap ways to quiet them are the owner's (see the section above): the sound for the person's own ants only (silences every bot's loop and costs the bots nothing; application only), and the quiet raid rule (`raid_min_free` 2, 5 percent of the score at Medium against bots that raid at one free tile). An engine change that stops the clip's re-order after the n-th failure is not recommended: it departs from the original.
- **The bots' own refused orders that remain** (a match and seat at Hard: TREASURE 3.66, SMALL 1.14, GAUNTLET 1.04, MEDIUM 0.63, TINY 0.31, ISLANDS 0.26; AI22.10 bounds TREASURE at 9 in 1,000 orders): an own ant that waits on the ramp behind another and is drawn as Walking (the view cannot tell a waiting walker from a moving one, so the gate cannot know that the ramp is held), a thief's special order onto an enemy entrance or hole that shuts while the thief walks to it, and an attack at a target that no walk reaches.
- **Not in this batch:** the flower play and Swimmers on TREASURE (the roadmap in `docs/BOTS.md`) come after it, on their own branches.
