# Bots: the can't-go batch (the evidence)

What the batch does, the owner's decisions, the numbers and what is left are in `docs/BOTS.md`, "The can't-go loop". This note keeps the evidence behind them: the study, the checks and the mutants.

## The study

The owner heard "Can't go there." a lot ("it keeps trying to do illegal moves"; "I was hearing it a lot on the small map"). A measurement of 84 matches of the v0.5.0 bots (a scratch tool on the built libraries, no change to the code) found:

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

71 faults of the new code, one at a time; the unmutated tree was built and tested before the first fault, every 25 faults and after the last (all passed). All 71 were caught:

| Fix | Faults | Caught by |
|---|---|---|
| The counter: what is counted (both texts, the window of 10 ticks, began against repeated, orders, refused) | 17 | AI22.1 (5), AI22.2, AI22.3 (3), AI22.4 (2), AI22.5, AI22.9 (5) |
| CG1, the map as it is now: the field, `reaches_hill`, the gate, the rescue, the aid, the Swimmer | 18 | AI1.23 (7), AI10.8 (3), AI10.9 (2), AI10.10, AI10.11, AI10.13 (4) |
| CG2, the raid rule and `raid_min_free` | 20 | AI12.4 (19), the self-test of `bot_arena` (the key `raidfree`) |
| CG5, the ramp | 5 | AI10.12 (5) |
| The switch and its plumbing: `cantgo_aware` handed to the gate and to the economy, the plain economy of the stall detector, the defaults | 11 | AI10.14 (6), AI7.29 (2), AI10.10, AI12.4, AI22.10 |

Two of the 71 did not count at the first run and were run again with a corrected spec, and both were caught: one fault could not be built (an unused parameter under `-Werror`), one was run with a test that cannot see it (that the economy is handed the flag is seen by AI10.10, not by AI10.14, which plays the switch off). An earlier fault of the raid rule that survived was an equivalent one (a clamp of the minimum to at least 1: a hole with no free tile has no walk into it); the clamp was removed.


