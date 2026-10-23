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

Left alone: ants of a pick-up, a raid, the expedition or a fight, Fire Ants, Bombers, Thieves, Swimmers, and any ant that carries food. An enemy that is walking is only struck by an ant that already stands on the right tile. No job where three other enemy ants stand close to the tile. The walk is asked of the map as it is now (fire walls and bombs lit after the start count), at most half the job's clock. A job ends when the target is gone, has moved, no longer dies of the blow, or after 240 ticks (12 seconds); an enemy that was struck at or left is not tried again for 300 ticks. At most two jobs run at a time. The player does not look at anything the other players cannot (no fog of war is involved: bots are not played with fog, [BOTS.md](BOTS.md) rule 8).

## Fights that can be reached (`reach_fights`)

Over the 534 maps of the library, 30.8 percent of all orders of four Hard players were refused by the engine with "Can't go there": the free ants and the defenders were sent to an enemy on the other side of water that no walk crosses. The ant then stands in its can't-go clip and nothing is struck. Now an ant is only picked for a fight when the start analysis ([BOTS.md](BOTS.md)) says that a walk takes it next to the enemy (`walk_reaches`).

## Fights when there is nothing else to do (`idle_hunt`)

A fight ends, and none starts, while the seat has no more ants than a reserve (`fight_reserve`, one more without eggs). On the arena maps with three ants and no food (AntsTournament, Desert Swimming) the whole seat was reserve and it never moved in 8 minutes. Now, when no pile is within reach of the seat's hill and there is nothing to bank, the reserve is one ant.

## Fire walls only when they are worth it (`chore_worth`)

The ring of fire round an enemy gate (the sabotage, `SabotageTask`) is lit only while that team has food to bank: a pile its hill reaches, or an ant that carries food. Without the rule the ring was lit round hills that had nothing to gain from the gate; measured, switching the whole sabotage off changed the points of a Hard player by +9 (plus or minus 40) over 300 matches. The walls of the player's own hill (`WallTask`) are not touched: they are worth a lot (on TREASURE 1,808 points).

## Switches and measuring

All four are in the Hard plan only (`LevelPlan::opportunity`, `reach_fights`, `idle_hunt`, `chore_worth`; `opportunity_reach` 8, `opportunity_jobs` 2, `opportunity_ticks` 240). `bot_arena --tune kill=0` takes all four out and plays the old Hard player exactly (the same state hash in every match); `opp=0`, `reach=0`, `idlehunt=0`, `worth=0` take one out; `oppreach=`, `oppjobs=`, `oppticks=` set the numbers.

`bot_watch` (`tools/bot_watch.cpp`, target `bot_watch`) plays matches like `bot_arena`, and counts per seat and per minute what the bots do badly, from outside the state: idle ants and how long, ants that do not get anywhere, orders the engine refuses, enemy ants that stood where a blow from the far side would have killed them while a free ant of the seat stood near (and whether the seat took the chance), carriers that were passed, walls and rings of fire, bombs that hit the seat's own ants. It reads the engine and the orders and never writes back. `--out FILE` writes a JSON report (the counts of every seat, per minute, and the worst scenes); `--replay FILE` counts the same over recordings. A match under watch ends on the same state hash as the same match in `bot_arena`.

## What was measured

(Numbers are filled in at the end of the work.)
