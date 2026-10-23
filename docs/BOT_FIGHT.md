# Hard bots: the quick kill, fights that can be reached, and chores that are worth it

The Hard computer player ([BOTS.md](BOTS.md)) took too few chances to kill an ant, and stood around on many maps. This page says what was found, what the Hard player does now, and what was measured. Easy and Medium are not changed.

## What the engine does with a blow

A blow throws the victim away from the attacker, in a straight line through the victim's tile: one tile for a worker's blow, four for a Combat Ant's punch (`src/ants_sim/combat_system.cpp`: `knock_dir`, `landing_ok`, `start_knock_flight`). Where the ant lands decides what the blow does:

| landing | what happens |
|---|---|
| water | the victim drowns (a Swimmer does not) |
| a bomb | it loses 2 more hit points and is thrown again, four tiles |
| a fire wall | it loses 1 more hit point |
| anywhere else | it is on its feet again, with the hit point the blow took (1; a Combat Ant's punch takes 2) |

When the line is shut (a rock, the edge of the map) the engine tries the direction next to it (+1, -1, +2, -2 steps of 45 degrees) before the blow lands the ant on the spot. An ant cannot be attacked from, or on, a water tile, so a victim at the water's edge is only reached from the land side, and the throw goes into the water only when the attacker stands on the side away from it.

So an enemy that stands next to water dies of ONE blow from the right side, and lives through the same blow from any other. An ordinary attack order lets the ant walk its own way and strike from wherever it arrives, which is why the earlier hunt (`water_hunt`, `water_push_blows`) only worked when an ant happened to stand in line.

`include/ants_ai/knock.hpp` has these rules as pure functions of the grid (`knock_flight`, `kill_shot`); the test AI31.1 plays 240 random grounds against the engine itself and the model must agree with it every time.

## The quick kill (`OpportunityTask`)

Rank 5, looked at before the fights and before every chore. For each enemy ant it tries the eight tiles round it as the tile to strike from, and asks `kill_shot`: does ONE blow from there kill it (water for an ant that cannot swim, a bomb or a fire wall with the hit points left, or a victim that has no more hit points than the blow takes)? The nearest ant that is free, or does a chore (the economy, a guard, mines, counters of bombs), walks to that tile with a plain move, then strikes. Water is worth most, a bomb or a fire wall less, a plain finish least; a farther striker is worth less (`worth - 25 * distance`).

Left alone: ants of a pick-up, a raid, the expedition or a fight, Fire Ants, Bombers, Thieves, Swimmers, and any ant that carries food. An enemy that is walking is only struck by an ant that already stands on the right tile. No job where three other enemy ants stand close to the tile.

A trip has to be worth it. Measured over 4,600 jobs of the first version, 7 in 10 ended with no blow: the enemy had stopped for a moment and walked on before the striker arrived. So the Hard player goes only
- for an enemy that has stood on its tile for one second (`opportunity_still`, 20 ticks), unless an ant already stands on the right tile, which strikes at once;
- not for a Combat Ant at rest (`opportunity_skip_guard`; its reflex meets the striker on the way: 1 or 2 jobs in 100 ended in a kill);
- with an ant of the economy (the harvesters) only for a kill that is two tiles away (`opportunity_harvest_d`); free ants and ants that do a chore go up to eight tiles. The walk is asked of the map as it is now (fire walls and bombs lit after the start count), at most half the job's clock. A job ends when the target is gone, has moved, no longer dies of the blow, or after 240 ticks (12 seconds); an enemy that was struck at or left is not tried again for 300 ticks. At most two jobs run at a time. The player does not look at anything the other players cannot (no fog of war is involved: bots are not played with fog, [BOTS.md](BOTS.md) rule 8).

## Fights that can be reached (`reach_fights`)

Over the 534 maps of the library, 30.8 percent of all orders of four Hard players were refused by the engine with "Can't go there": the free ants and the defenders were sent to an enemy on the other side of water that no walk crosses. The ant then stands in its can't-go clip and nothing is struck. Now an ant is only picked for a fight when the start analysis ([BOTS.md](BOTS.md)) says that a walk takes it next to the enemy (`walk_reaches`).

## Fights when there is nothing else to do (`idle_hunt`)

A fight ends, and none starts, while the seat has no more ants than a reserve (`fight_reserve`, one more without eggs). On the arena maps with three ants and no food (AntsTournament) the whole seat was reserve and it never moved in 8 minutes. Now, when no pile is within reach of the seat's hill on foot and there is nothing to bank, the reserve is one ant. Not when food lies beyond water (the Swimmers have that work: the island play) or behind a gate or a field (the gate goals have it): there the ants do not go hunting.

## Fire walls only when they are worth it (`chore_worth`)

The ring of fire round an enemy gate (the sabotage, `SabotageTask`) is lit only while that team has food to bank: a pile its hill reaches, or an ant that carries food. Without the rule the ring was lit round hills that had nothing to gain from the gate; measured, switching the whole sabotage off changed the points of a Hard player by +9 (plus or minus 40) over 300 matches. The walls of the player's own hill (`WallTask`) are not touched: they are worth a lot (on TREASURE 1,808 points).

## Switches and measuring

All four are in the Hard plan only (`LevelPlan::opportunity`, `reach_fights`, `idle_hunt`, `chore_worth`; `opportunity_reach` 8, `opportunity_jobs` 2, `opportunity_ticks` 240, `opportunity_still` 20, `opportunity_skip_guard`, `opportunity_harvest_d` 2). `bot_arena --tune kill=0` takes all four out and plays the old Hard player exactly (the same state hash in every match); `opp=0`, `reach=0`, `idlehunt=0`, `worth=0` take one out; `oppreach=`, `oppjobs=`, `oppticks=`, `oppstill=`, `oppguard=`, `oppharv=` set the numbers. Medium and Easy are not changed (they still send orders at enemies across water; that is the same fault, left for a later change).

`bot_watch` (`tools/bot_watch.cpp`, target `bot_watch`) plays matches like `bot_arena`, and counts per seat and per minute what the bots do badly, from outside the state: idle ants and how long, ants that do not get anywhere, orders the engine refuses, enemy ants that stood where a blow from the far side would have killed them while a free ant of the seat stood near (and whether the seat took the chance), carriers that were passed, walls and rings of fire, bombs that hit the seat's own ants. It reads the engine and the orders and never writes back. `--out FILE` writes a JSON report (the counts of every seat, per minute, and the worst scenes); `--replay FILE` counts the same over recordings. A match under watch ends on the same state hash as the same match in `bot_arena`.

## What was measured

Every figure is from `bot_watch` or `bot_arena` matches of 8 minutes, the new Hard player against the old one (`kill=0`) in the same matches: four players, two of each, every arrangement of the seats (or two players, both orders); a figure is a difference with its 95 percent interval (normal approximation over the matches). "Win" is the share of matches in which the seat scores more than the best seat of the other kind (ties count a half). Maps: the 6 shipped ones; 9 community maps that the work was tuned on (water, fire walls, bombs: !!!BLOCKADES!!!, !-Midz-!, !SpY VS sPy!, Alley Ants, Ants World, Arena101, Bomb Island, CRAZY 1, ant party); 10 community maps that were first held out (!!!Bridges v4!!!, A&E, ALL IN ONE !, ANT ZONE, ARMIES OF FATE, Acid Blood, Ant's Island, Bunker, aNtS_oF_1998, anthology grid; the first version lost on them, and the rules above were made for it, so they are no longer held out); and all 534 maps of the library, one seed each, two arrangements (the last check, on maps that were mostly never looked at).

| | matches | kills per seat (old, new) | enemy ants drowned on purpose per seat (old, new) | win, new minus old | points, new minus old |
|---|---|---|---|---|---|
| four players, shipped maps | 576 | 1.77, 1.81 | 0.18, 0.21 | -0.007 +-0.059 | +3 +-25 |
| four players, 9 tuned maps | 648 | 2.75, 3.62 | 0.38, 0.84 | +0.034 +-0.043 | +3 +-37 |
| four players, 10 first held-out maps | 960 | 5.10, 7.12 | 1.55, 2.66 | -0.015 +-0.034 | -32 +-49 |
| four players, 534 maps of the library | 1,068 | 2.74, 3.36 | 0.31, 0.51 | +0.005 +-0.019 | (see the text) |
| two players, shipped maps | 360 | 1.05, 0.98 | 0.08, 0.08 | -0.008 +-0.103 | -34 +-70 |
| two players, 9 tuned maps | 432 | 1.85, 2.14 | 0.19, 0.37 | +0.134 +-0.070 | +338 +-139 |
| two players, 10 first held-out maps | 320 | 4.39, 5.64 | 1.23, 1.68 | +0.062 +-0.081 | +100 +-224 |

So the player kills more (a quarter more over the library, a third to two fifths more on the community maps, and it drowns on purpose about twice as many enemy ants on the water maps), and loses fewer ants of its own (3.49 to 3.33 per seat over the library), while the points do not change where the whole field plays (four players), and rise where two play on the community maps. Over the library the points of a few maps with huge negative scores decide the mean, so the win share is the figure: 147 maps went better, 160 worse and 38 equal, among the 345 where anyone scores. Against the Medium player the new Hard wins as the old one did: in a duel 0.583 +-0.103 (old 0.567) on the shipped maps, 0.176 +-0.099 (old 0.157) on the tuned maps, 0.308 +-0.084 (old 0.325) on the others.

The first version of the quick kill (every still enemy, 8 tiles, any Combat Ant) lost: -0.077 +-0.034 on the first held-out maps and -0.027 +-0.020 over the library. The lesson is in the numbers of its jobs: of 4,600 jobs 7 in 10 ended with no blow because the enemy had only stopped for a moment, and a fifth of the ants taken for them were the economy's. The three rules of "A trip has to be worth it" and the hunting rule for food beyond water made it break even; the points of the pairs are noise at this size (a map is +-150 points with 100 matches), so nothing finer than that is claimed.

What did not improve: the share of ant time spent standing idle did not fall, it rose (37 percent to 47 percent over the library). The orders that the engine refused fell from 30.5 to 19.0 percent of all orders, the ants in the can't-go clip from 4.5 to 2.7 percent of their time and the stuck ants from 6.3 to 4.2 percent, but an ant that is no longer sent to an enemy across the water stands. Of the idle time, nearly all is on maps where nothing can be done (no food that the ants reach, no enemy they can walk to, or the last ants of a decided match): the idle time with a pile that the hill reaches on foot is 2.6 percent (old) and 3.1 percent (new) of the ant time, 63 maps over 5 percent in both, mostly maps with a pond or a gate. Those are the work of the island play and the gate goals ([BOT_GATES.md](BOT_GATES.md)), not of this change.
