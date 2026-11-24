# When the food is gone, and the idle swap

The food of a match is finite and never comes back, so every match has a moment after which no ant can bank anything by eating. A computer player of the standard bot ([BOTS.md](BOTS.md#the-standard-bot)) used to stand from then on: its economy had nothing to send its ants to, and the fighting tasks were built around the economy (the last ants stay out of every fight, an assault asks for the surplus of the economy, a raid waits for a big box). This page is the rule set that gets the ants moving again, the numbers of the three levels, what was measured and what is not built. Source: `include/ants_ai/tactics.hpp` (`LevelPlan::gone_*`, `idle_swap*`, `Tactics::gone`), `src/ants_ai/standard_bot.cpp`, `standard_tasks.cpp`, `standard_tasks_raid.cpp`, `standard_tasks_powerup.cpp`, `heal_task.cpp`; tests `AI56` (`tests/test_ai/test_ai_gone.cpp`), mutants `tests/data/bot_gone_mutants.json`. `bot_arena --tune gone=0` is the bot as it was before (the same match, hash for hash, as the bot without this change).

## What the idle ants were doing

Studied with `bot_watch` over a recording of a computer-only match on POPcOrN (4 bots, two teams of two, 14 minutes), with the arena's idle tally ([below](#measuring-it)) over many matches of the rotation's maps, and by replaying the recording tick by tick.

- **The size of it.** On POPcOrN a team starts with three ants, dealt across both halves of the map ([BOT_GATES.md](BOT_GATES.md#popcorn-in-a-match)), and is down to one to three ants after the first minutes: about half of all ant time is idle (58 percent for the seat of the recorded match that banked nothing, 10 to 32 percent for the others). After the last food has been taken, the bot as it was stands for 84 to 97 percent of its ant time on POPcOrN, 89 to 94 percent on ISLANDS and 54 to 77 percent on SMALL (the old columns below). In the recorded match all four teams ended with one ant each, standing, with two Fire power-ups and eight Thief power-ups lying on the map.
- **Typed ants stood beside power-ups they could take.** `PowerUpTask` sent plain ants only, and a bot whose ants are all typed (on POPcOrN nearly every ant of a team is, after the first minute) fetched nothing for the rest of the match. An ant that takes a power-up while it has a type keeps the new one and leaves its old one on a free tile beside it ([GAMEPLAY.md](GAMEPLAY.md), "Typed ants"; the bots' own tasks do it on POPcOrN, a Bomber becoming a Swimmer and then a Fire Ant). In the recording a Bomber stood more than two minutes on the left island, on the tile next to a Fire power-up, and nothing sent it: no bridge was missing, the power-up was one step away. A bridge to a power-up that a Swimmer could dig was not the cause here (an earlier trial of the bridge planner of the island play on POPcOrN, 20 seeds of four standard bots, found no job there, and its switch was taken out again; it was not repeated for this page).
- **After the last food nothing was left to do.** The idle ants after the last food are Swimmers that ferried food, Thieves with no hill that has points to take, Bombers and Fire Ants whose task is done. A hurt ant stands where it was hurt, because a fight needs four hit points and nothing sent it home. The fighting tasks did not take a lone ant at all: `FightTask` kept the last ants (two, or one with `idle_hunt`) out of every fight, started an assault only with at least two ants of the economy's surplus, and took only workers and Combat Ants.

## The rules

The batch begins for a bot when it **has nothing to do** and has had nothing for `gone_wait` ticks, with more than 300 ticks left, in the original's game (the game type 187 has no food by its rules; its brawl is its own, [BOT_187.md](BOT_187.md)). It has nothing to do when no pile that its hill walks to has units left (a lunchbox that no walk joins to the hill is no pile), and either no pile is left at all, or the island, gate or field work has banked nothing for `gone_stall` ticks after the first 3 minutes (a map whose food is behind a wall of fire or beyond water keeps its work as long as it banks). The bot's own stall detector and the tasks of the gate goals and the island play are not changed; they run as before.

While it is on, and only then:

1. **Steal.** A Thief raids a hill for `gone_raid_min` points instead of the level's usual loot (a steal is a swing of twice the loot), Easy too, and the highest box is still the first target. The team wants a Thief while an enemy box shows that much and the own hill's ants walk to that enemy hill (`gone_thief`; a Thief that no walk leads to a hill is of no use), and the trip to a Thief power-up may be as long as `gone_trip_ticks`. An idle Swimmer that no task holds (the ferry is over) may be the ant that goes and becomes the Thief (`gone_swap`; Medium and Hard); a Fire Ant is the keeper of the walls against the thieves and a Bomber lays the mines, so neither is taken.
2. **Fight only the fights that are won.** An assault (the free ants go for an enemy ant) needs no wait and no surplus (two free ants are enough, as in the level's own assault, and the last ants are held by rule 3) but asks for `gone_odds_percent` of the strength near the target, not the level's usual odds: with the food gone a fight changes no score, and a lost one costs ants, and a team's last ant costs a forced hatch. A team that shows at most `gone_wipe_ants` ants and a box of `gone_wipe_box` points is the first target, at the lower `gone_wipe_odds_percent`: killing its last ant forces a hatch of min(box, 200) points.
3. **The last ant.** The last ants of a bot take part only in an assault on a team that has more to lose than the bot (min(its box, 200) at least `gone_margin` points above min(the own box, 200): the price of a wipe-out) or that is ahead of it by twice that margin (a team that is behind loses no place by losing a fight). A bot that is ahead stands: a lost fight costs it and a won one changes no place. Easy never risks its last ant.
4. **Heal.** An idle ant with at most `gone_heal_hp` hit points walks into its hill from wherever a walk leads (`HealTask`, which is otherwise off): healthy ants are what the assault and the raids take.

**The idle swap** (any time of the match, Medium and Hard, `idle_swap`): an own Bomber or Swimmer that no task holds and that has stood idle with empty hands for `idle_swap_ticks` takes a Fire power-up that lies within a trip while the bot has no Fire Ant (the order of value of the pick-up trips, `PowerUpTask`: Fire, Bomber, Thief, Combat, Swimmer). The ant leaves its old power-up beside it, so the Bomber's hat can be taken again by a plain ant.

| | Easy | Medium | Hard |
|---|---|---|---|
| begins after nothing to do for (`gone_wait`) | 1,600 ticks (80 s) | 800 (40 s) | 400 (20 s) |
| work that banks nothing is lost after (`gone_stall`, never before 3 minutes) | 3,000 | 1,800 | 1,200 |
| raids for (`gone_raid_min`) | 30 | 15 | 10 |
| trip to a Thief (`gone_trip_ticks`) | 500 | 700 | 900 |
| an idle Swimmer becomes the Thief (`gone_swap`) | no | yes | yes |
| assault with free ants (`gone_assault_min`) | 2 | 2 | 2 |
| strength asked for (`gone_odds_percent`) | 250 | 200 | 200 |
| against a team that a wipe-out forces to hatch (`gone_wipe_odds_percent`) | 250 | 100 | 80 |
| last ants, margin (`gone_margin`) | never | 100 | 50 |
| first target: a team of at most (`gone_wipe_ants`) | none | 1 ant | 2 ants |
| heals at or below (`gone_heal_hp`) | 5 | 6 | 7 |
| idle swap after standing idle for (`idle_swap_ticks`) | no | 600 ticks (30 s) | 300 (15 s) |

The weaker the level, the later it begins and the more it asks before it risks anything: the same reasoning as the rest of the plans ([BOTS.md](BOTS.md#difficulty-levels)). Everything it sends is an ordinary command of a person (a special order onto a hill, an attack, a move onto the hill's entrance or onto a power-up), through the controller's budget and reaction delay, from what the bot's team can see ([BOTS.md](BOTS.md#fairness-in-detail)); the simulation, the lock-step rules and every state hash are untouched. The tuning keys of `bot_arena --tune`: `gone` (0: off, and the idle swap with it), `gonewait`, `gonestall`, `goneraid`, `gonethief`, `goneswap`, `gonetrip`, `gonemin`, `goneodds`, `gonewipeodds`, `gonemargin`, `gonewipe`, `goneheal`, `iswap`, `iswapticks`.

## Measuring it

`bot_arena` counts, per seat and match, from the engine's own state every 4th tick (`IdleTally`, report format 7): the ants and the idle ants in ticks, the same two from the last food taken to the end (the last look at which the units on any pile fell; the whole match when none were taken), and the raids that the seat's Thieves began with the points they took (`idle` in the seat's record, `last_food` in the match's). An ant is idle when it stands in an idle clip with no order and nothing in its hands for 5 seconds. `--teams A+B` starts a match with seats A and B as a team and the other two as the other team, as a room and the game do. The kills, losses, hatched eggs, power-ups taken, banked and raided points were in the report already.

## Measured

**How.** The bot of this page against the bot as it was, in the same matches: seats 0 and 2 play the new bot (`--seat N=standard+gone=1:LEVEL`) and seats 1 and 3 the old one (`standard+gone=0:LEVEL`), as two teams (`--teams 0+2`) and then with the seats exchanged, and in free for all with the new bot on seats 0 and 1 and then on 2 and 3. Both sides are given a tuning, because a plain `standard:LEVEL` draws the registry's style and a tuned one does not: a plain seat against a tuned one measures the style, not the change. Each row is 20 seeds in four arrangements, 80 matches (the rows of 100 seeds: 400), every one replay-checked and free of errors. "New" columns come first, "old" second; the points are a seat's own score; idle is the share of ant time that ants stood idle for 5 seconds or more, from the last food taken to the end and in the whole match.

*By level, over the nine maps of the rotation (TINY, SMALL, MEDIUM, GAUNTLET, TREASURE, ISLANDS, POPcOrN, HELLRAISER, Bombz Away):*

| | matches | new seats win | points a seat, new / old | forced hatches | ants lost | kills | raided points | idle after the last food, % | idle all the match, % | power-ups taken |
|---|---|---|---|---|---|---|---|---|---|---|
| easy | 720 | 50% | 782 / 782 | 1307 / 1317 | 2677 / 2720 | 927 / 929 | 2300 / 2350 | 67 / 67 | 41 / 41 | 1200 / 1193 |
| medium | 720 | 51% | 924 / 923 | 364 / 372 | 2164 / 2343 | 1412 / 1262 | 53550 / 53650 | 45 / 47 | 25 / 26 | 4551 / 4412 |
| hard | 720 | 50% | 926 / 921 | 415 / 420 | 2884 / 2944 | 2035 / 2005 | 90750 / 91950 | 66 / 66 | 25 / 25 | 5347 / 5272 |

*By map, Medium and Hard (Easy has the same shape as its row above):*

| | matches | new seats win | points a seat, new / old | forced hatches | ants lost | kills | raided points | idle after the last food, % | idle all the match, % | power-ups taken |
|---|---|---|---|---|---|---|---|---|---|---|
| TINY medium | 80 | 50% | 1207 / 1207 | 0 / 0 | 10 / 18 | 18 / 10 | 0 / 0 | 18 / 16 | 4 / 4 | 0 / 0 |
| TINY hard | 80 | 49% | 1188 / 1188 | 15 / 15 | 113 / 115 | 106 / 122 | 0 / 0 | 15 / 13 | 5 / 4 | 0 / 0 |
| SMALL medium | 80 | 51% | 962 / 958 | 2 / 2 | 135 / 163 | 155 / 137 | 0 / 0 | 60 / 64 | 21 / 22 | 489 / 484 |
| SMALL hard | 80 | 52% | 986 / 979 | 4 / 6 | 254 / 266 | 259 / 248 | 0 / 0 | 73 / 76 | 19 / 20 | 611 / 596 |
| MEDIUM medium | 80 | 55% | 1214 / 1203 | 7 / 15 | 423 / 472 | 437 / 394 | 4000 / 3950 | 19 / 20 | 3 / 3 | 107 / 101 |
| MEDIUM hard | 80 | 50% | 1185 / 1172 | 7 / 11 | 466 / 522 | 504 / 465 | 25085 / 25635 | 28 / 29 | 4 / 4 | 445 / 434 |
| GAUNTLET medium | 80 | 51% | 357 / 357 | 8 / 9 | 383 / 478 | 468 / 388 | 20715 / 20715 | 20 / 21 | 5 / 5 | 404 / 403 |
| GAUNTLET hard | 80 | 50% | 278 / 277 | 23 / 24 | 597 / 598 | 594 / 593 | 39975 / 39975 | 27 / 27 | 2 / 2 | 721 / 721 |
| TREASURE medium | 80 | 50% | 2063 / 2063 | 0 / 0 | 215 / 215 | 215 / 215 | 21735 / 21735 | 20 / 20 | 1 / 1 | 689 / 689 |
| TREASURE hard | 80 | 50% | 2132 / 2132 | 0 / 0 | 347 / 352 | 352 / 345 | 15590 / 15590 | 36 / 36 | 1 / 1 | 758 / 760 |
| ISLANDS medium | 80 | 50% | 1399 / 1399 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 89 / 89 | 63 / 63 | 1240 / 1240 |
| ISLANDS hard | 80 | 50% | 1401 / 1400 | 0 / 0 | 90 / 78 | 71 / 85 | 0 / 0 | 94 / 94 | 56 / 56 | 1296 / 1288 |
| POPcOrN medium | 80 | 50% | 551 / 557 | 85 / 84 | 367 / 366 | 72 / 71 | 3700 / 3850 | 72 / 84 | 44 / 47 | 1272 / 1145 |
| POPcOrN hard | 80 | 50% | 609 / 584 | 73 / 71 | 343 / 339 | 46 / 44 | 6800 / 7450 | 84 / 86 | 49 / 51 | 1165 / 1122 |
| HELLRAISER medium | 80 | 50% | 50 / 51 | 0 / 0 | 49 / 49 | 47 / 47 | 3400 / 3400 | 49 / 50 | 47 / 47 | 350 / 350 |
| HELLRAISER hard | 80 | 50% | 38 / 38 | 0 / 0 | 61 / 61 | 61 / 61 | 3300 / 3300 | 53 / 53 | 51 / 51 | 351 / 351 |
| Bombz Away medium | 80 | 50% | 510 / 510 | 262 / 262 | 582 / 582 | 0 / 0 | 0 / 0 | 23 / 28 | 2 / 2 | 0 / 0 |
| Bombz Away hard | 80 | 50% | 520 / 520 | 293 / 293 | 613 / 613 | 42 / 42 | 0 / 0 | 36 / 36 | 4 / 4 | 0 / 0 |

*Larger samples where the rows above differ most: 100 seeds, 400 matches a row:*

| | matches | new seats win | points a seat, new / old | forced hatches | ants lost | kills | raided points | idle after the last food, % | idle all the match, % | power-ups taken |
|---|---|---|---|---|---|---|---|---|---|---|
| POPcOrN medium | 400 | 50% | 553 / 551 | 407 / 375 | 1840 / 1796 | 333 / 336 | 19700 / 22050 | 73 / 84 | 44 / 47 | 6431 / 5802 |
| POPcOrN hard | 400 | 51% | 591 / 598 | 366 / 340 | 1752 / 1728 | 200 / 204 | 35850 / 40050 | 81 / 85 | 49 / 51 | 6221 / 6057 |
| MEDIUM medium | 400 | 52% | 1213 / 1204 | 38 / 60 | 2160 / 2408 | 2244 / 2026 | 24860 / 25960 | 18 / 19 | 3 / 3 | 565 / 519 |
| MEDIUM hard | 400 | 50% | 1189 / 1186 | 32 / 35 | 2296 / 2326 | 2269 / 2249 | 146815 / 147665 | 29 / 30 | 4 / 4 | 2123 / 2108 |
| TREASURE medium | 400 | 50% | 2049 / 2049 | 2 / 2 | 1168 / 1168 | 1164 / 1164 | 112570 / 112570 | 21 / 21 | 1 / 1 | 3469 / 3469 |
| TREASURE hard | 400 | 50% | 2131 / 2130 | 1 / 3 | 1737 / 1756 | 1739 / 1724 | 90035 / 90085 | 29 / 30 | 2 / 2 | 3848 / 3850 |

**What the numbers say.**

- **Nothing got worse.** The new seats win 50, 51 and 50 percent of the matches at Easy, Medium and Hard, and the points of a seat are those of the old seats on every map and level (the largest gaps, POPcOrN and MEDIUM, are within a few points in the 400 matches). The forced hatches are 1,307 against 1,317, 364 against 372 and 415 against 420.
- **The fights are better at Medium.** The new Medium seats lose 2,164 ants against 2,343 and take 1,412 kills against 1,262; at Hard 2,884 against 2,944 and 2,035 against 2,005. A fight is taken only with the lead that the table of rules asks for.
- **The ants stand less where something can be done.** After the last food: POPcOrN 72 against 84 percent at Medium (73 against 84 in 400 matches), SMALL 60 against 64 and 73 against 76, Bombz Away 23 against 28; the new seats take more power-ups (4,551 against 4,412 at Medium, 5,347 against 5,272 at Hard; POPcOrN 6,431 against 5,802 in 400 Medium matches). Over all nine maps the idle share after the last food moves little (45 against 47 at Medium, 66 against 66 at Hard), because on ISLANDS, HELLRAISER, GAUNTLET and TREASURE no ant can reach anything that is worth a walk once the food is gone: the rows there are the same in both columns.
- **Steals are not more.** The raided points are 53,550 against 53,650 at Medium and 90,750 against 91,950 at Hard: the Thieves of the old bot already raid on the maps that have them, and the batch changes the loot they accept, not the hills they can walk to.
- **POPcOrN costs a few forced hatches** (407 against 375 at Medium, 366 against 340 at Hard, in 400 matches) for the same points and the same wins: the new seats lose 2 percent more ants there.

The tallies are the arena's (`IdleTally`, report format 7); a run that wants them is `bot_arena --map NAME --seeds 1..20 --seat 0=standard+gone=1:hard --seat 1=standard+gone=0:hard ... --teams 0+2 --replay-check --out report.json`.

## Tried and left out

- **Idle typed ants in the fights** (built, measured, taken out). A Swimmer, Bomber or Fire Ant that stands idle joined an assault or a hunt like a worker. On POPcOrN, where a team has one to three ants and every walk crosses mines and fire, it cost forced hatches and took nothing: in 80 matches (the bot with it against the bot as it was, same matches, both tuned) 109 forced hatches against 87 at Medium and 85 against 71 at Hard; the same plan with the typed ants kept out of the fights, 88 against 85 and 73 against 70.
- **A Thief from any idle typed ant.** Only an idle Swimmer becomes the Thief: the Fire Ant keeps the walls against the thieves and the Bomber lays the mines. Not measured on its own.
- **A bridge to a power-up on a map without island play.** The planner of "Bridges for ants" was run on POPcOrN behind a switch and found no job in 20 seeds (no other map without island play has Swimmers to dig); it was taken out again. A bot still digs such bridges on ISLANDS (Hard).

## Not built, and not verified

- **ISLANDS.** No hill walks to another there, so nothing can be stolen on foot once the food is gone; the batch begins and finds nothing to steal or fight (the rows of the table are the same in both columns).
- **A stranded ant.** A hurt Bomber stood five minutes on the middle island of POPcOrN beside the pile, with no walk home: it would carry food only as a Swimmer, and Swimmer power-ups lay on that island at times. The idle swap takes a Fire power-up only; a swap to the Swimmer for the ferry is not built.
- **A seat that banks nothing for a whole match** (the bottom seats of POPcOrN): what keeps it from banking, and what changed for the batch, is in [BOT_GATES.md](BOT_GATES.md#why-seats-bank-nothing). Its work is the breach's, which this page does not touch; the batch begins for it after the first 3 minutes plus `gone_stall` and no longer waits out a stall detector's fallback that lunchboxes in the field had caused, and it has nothing to send where no walk leads to a hill or an enemy ant.
- **Guard and help.** Fire walls at the thief hole and the answer to a blow on an ally are the older rules ([BOTS.md](BOT_TACTICS.md#defence), [Alliances](BOT_PLAY.md#alliances)); the batch adds nothing to them.
- **Not measured:** matches with a person against the bots, Fog of War (a bot with fog is refused), the 187 game type (no batch there), the 540 maps of the players beyond the three of the rotation's library.
