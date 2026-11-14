# Hard bots: the quick kill, fights that can be reached, and chores that are worth it

The Hard computer player ([BOTS.md](BOTS.md)) took too few chances to kill an ant, and stood around on many maps. This page says what was found, what the Hard player does now, and what was measured. Easy and Medium are not changed (except for the counters of the section on hazards on another hill's ground, which Medium shares).

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
- with an ant of the economy (the harvesters) only for a kill that is two tiles away (`opportunity_harvest_d`); free ants and ants that do a chore go up to eight tiles. The walk is asked of the map as it is now (fire walls and bombs lit after the start count), at most half the job's clock. A job ends when the target is gone, has moved, no longer dies of the blow, or after 240 ticks (12 seconds); an enemy that moved away before the blow is not tried again for 40 ticks, one that was struck at (or whose job ran out) not for 300 ticks. At most two jobs run at a time. A striker that the walk of the moment does not take to a tile in time is not searched again for that tile for 100 ticks, and one look makes at most two searches of the whole map. The last ants of a seat (the reserve of a fight) go only for a kill that drowns, and not even that while food lies beyond water or behind a gate or a field: they are the crews of that work. The player does not look at anything the other players cannot (no fog of war is involved: bots are not played with fog, [BOTS.md](BOTS.md) rule 8).

## Fights that can be reached (`reach_fights`)

Over the 534 maps of the library, 30.7 percent of all orders of four Hard players were refused by the engine with "Can't go there": the free ants and the defenders were sent to an enemy on the other side of water that no walk crosses. The ant then stands in its can't-go clip and nothing is struck. Now an ant is only picked for a fight when the start analysis ([BOTS.md](BOTS.md)) says that a walk takes it next to the enemy (`walk_reaches`).

## Fights when there is nothing else to do (`idle_hunt`)

A fight ends, and none starts, while the seat has no more ants than a reserve (`fight_reserve`, one more without eggs). On the arena maps with three ants and no food (AntsTournament) the whole seat was reserve and it never moved in 8 minutes. Now, when no pile is within reach of the seat's hill on foot and there is nothing to bank, the reserve is one ant. Not when food lies beyond water (the Swimmers have that work: the island play) or behind a gate or a field (the gate goals have it): there the ants do not go hunting.

## Fire walls only when they are worth it (`chore_worth`)

The ring of fire round an enemy gate (the sabotage, `SabotageTask`) is lit only while that team has food to bank: a pile its hill reaches, or an ant that carries food. Without the rule the ring was lit round hills that had nothing to gain from the gate; measured, switching the whole sabotage off changed the points of a Hard player by +9 (plus or minus 40) over 300 matches. The walls of the player's own hill (`WallTask`) are not touched: they are worth a lot (on TREASURE 1,808 points).

## Hazards on another hill's ground (`own_ground_counters`)

The counters (BOTS.md, "Defence") put out an enemy's fire wall with the bot's Fire Ant and defuse an enemy's bomb with its Bomber, or set it off with a worker, wherever it lies within 10 tiles of the bot's own hill or within 4 tiles of a pile's tile (the pile radius 3, plus one) that its hill reaches. They did not ask whose ground that was. A bomb hurts only the ant that steps on it, a fire wall only the ant that walks into it, and neither changes any score: a hazard in another team's base costs the bot nothing until the bot walks there. Trips into such a base cost ants (the walk crosses the other bombs of the yard) and took the Fire Ant that keeps the walls of the bot's own thief hole away from home. A team that shut its hole with fire and mined its yard had the Fire Ant and the Bomber of every other bot walking across the map to it.

Medium and Hard now ask where a hazard lies (`hazard_ground`, `src/ants_ai/tactics.cpp`). It is

- **own** within 6 tiles of the bot's hill, and on any tile that no other hill walks to for less than the bot's hill does (the walking costs of the map analysis; the ally's hill and the hill of a team that dropped out are not other hills);
- **claimed** beside a pile with food (within 4 tiles of the pile's tile) that the bot has a claim to: its walk there costs at most 170 percent of the cheapest walk of any hill, or one of its ants carries food or harvests within 4 tiles of the pile. The work does not count within 10 tiles of another team's hill (`counter_rival_yard`): there only the walking cost makes a pile the bot's, so a bot that harvests the pile beside a rival's hill does not send its Bomber into that yard;
- **foreign** everywhere else, and always on the three tiles in front of another team's thief hole and on the ring round its gate. A team that dropped out is no rival: its yard is nobody's.

The bot clears a hazard only where the old rule found it in the way and `hazard_ground` says own or claimed. A bomb that no team owns (the map's own bombs of the walled maps) is not asked where it lies (it has no yard), but the rest of the rule below holds for it too. A bot that helps its ally (Medium, Defensive style) asks the same question for the ally's hill: the ally's own ground and the piles the ally has a claim to by walking cost are answered, a third team's yard never. The rest of the rule:

- a Bomber goes for a bomb with at least 5 hit points (two blasts take 4, and the way there may cross a second bomb); a worker that sets a bomb off needs 8, as before; the Fire Ant that keeps the walls is chosen with 3 hit points and answers an enemy wall with the same 3, because a fire wall burns the ant that walks into it and does not throw it onto a second bomb (a rule of 5 for it was measured on Ghetto and cost points, so it is not made);
- a worker sets a bomb off on purpose only on the bot's own ground and never as one of the last ants (`Tactics::reserve_of`); the Bomber and the Fire Ant do not go to a hazard beside a pile, off the own ground, when only the last ants are left;
- while the walls of its own thief hole are wanted, the Fire Ant that keeps them goes for no enemy wall farther than 12 tiles from the bot's hill. The leash and the rule of the last ants hold for a hazard in the ally's yard as well (that yard is not the bot's own ground).

The numbers are in `LevelPlan` (`counter_home_radius` 6, `counter_claim_percent` 170, `counter_work_tiles` 4, `counter_rival_yard` 10, `counter_keeper_leash` 12, `counter_bomber_hp` 5). Easy keeps the old counters. `bot_arena --tune ownground=0` plays the old counters exactly (the same state hash in every match); `grhome=`, `grclaim=`, `grwork=`, `grrival=` (0: the work claim reaches everywhere), `grleash=` and `grhp=` set the numbers.

**The bench bot `turtle`** (`tools/bench_turtle.hpp`, a seat for `bot_arena` and `bot_watch`, not a bot of the game) is the standard bot of Hard with every task switched off that sends an ant out of its own part of the map, and the mines round the hill on: it takes the Bomber and Fire power-ups of its side, keeps fire walls on its three hole tiles, lays bombs five to nine tiles round its hill, harvests its nearest piles, and never clears, raids or fights. `bot_watch` counts, for every seat, the defusals begun and the fires put out on ground that lies nearer another team's hill than its own, and the own ants that were blown up there (`defuses_at`, `outs_at`, `blasts_at`, by the team whose hill is nearest).

### What was measured for the counters

`bot_arena` (the old counters are `--tune ownground=0`: the same state hash as the build before the change in 192 matches). Four Hard bots of the same plan play each map (seeds 1 to 60; 1 to 180 on TREASURE); the table gives the mean score of a seat with its standard error, the scores of the four seats added (new minus old, per match, with the standard error over the matches), and the seats that ended under 800 points.

| four Hard bots | matches | score of a seat, old | new | four seats added, new minus old | seats under 800, old / new |
|---|---|---|---|---|---|
| TINY | 60 | 1189 (6) | 1189 (6) | +0 (0) | 0 / 0 of 240 |
| SMALL | 60 | 974 (9) | 976 (8) | +8 (17) | 17 / 14 of 240 |
| MEDIUM | 60 | 1127 (25) | 1133 (24) | +25 (21) | 40 / 42 of 240 |
| GAUNTLET | 60 | 268 (16) | 266 (15) | -9 (24) | 227 / 231 of 240 |
| ISLANDS | 60 | 1401 (9) | 1401 (9) | +0 (0) | 0 / 0 of 240 |
| TREASURE | 180 | 1950 (19) | 2026 (16) | +303 (70) | 34 / 11 of 720 |
| Dustbin | 60 | 190 (13) | 187 (14) | -12 (57) | 234 / 232 of 240 |
| Ghetto | 60 | 763 (72) | 955 (87) | +768 (269) | 153 / 146 of 240 |
| all eight maps | 600 | 1176 (16) | 1218 (17) | +169 (36) | 705 / 676 of 2400 |

On TINY and ISLANDS every match is the same as before. Nothing got worse: the two maps that lose a little (GAUNTLET, Dustbin) are inside one standard error. Ghetto and Dustbin are two of the walled maps ([BOT_GATES.md](BOT_GATES.md)); the map's own bombs there are asked as before. Four Medium bots (60 seeds each):

| four Medium bots | matches | score of a seat, old | new | four seats added, new minus old | seats under 800, old / new |
|---|---|---|---|---|---|
| TREASURE | 60 | 1995 (25) | 2045 (21) | +201 (73) | 5 / 3 of 240 |
| SMALL | 60 | 942 (10) | 942 (10) | +0 (7) | 53 / 51 of 240 |
| MEDIUM | 60 | 1195 (13) | 1194 (13) | -4 (6) | 4 / 4 of 240 |
| all three maps | 180 | 1377 (19) | 1394 (20) | +66 (25) | 62 / 58 of 720 |

Two new seats and two old seats in one match (every arrangement of the four over the seats, TREASURE, SMALL and MEDIUM, 30 seeds each, Hard): one of the two new seats wins 0.511 (standard error 0.021; TREASURE 0.544, SMALL 0.489, MEDIUM 0.500), the four scores of a seat pair 36 (standard error 18) points higher for the new seats of the 540 matches (even is 0.500). The same with Medium bots (15 seeds each, 270 matches): 0.498 (0.030; TREASURE 0.539, SMALL 0.478, MEDIUM 0.478), 40 (18) points higher.

The bench bot `turtle` on seat 0 against three Hard bots (100 seeds a map; per bot and match; `bot_watch`):

| turtle against three Hard bots | seeds | trips into the turtle's base (defusals and fires put out, old -> new) | own ants blown up in its yard | ants lost | score of a bot | bots under 800 |
|---|---|---|---|---|---|---|
| TREASURE | 100 | 3.15 (0.22) -> 0.02 (0.01) | 2.02 (0.12) -> 2.11 (0.13) | 1.84 (0.07) -> 1.74 (0.07) | 1841 (26) -> 1988 (20) | 11 -> 0 of 300 |
| SMALL | 100 | 0.02 (0.01) -> 0.01 (0.01) | 0.11 (0.03) -> 0.08 (0.02) | 1.20 (0.06) -> 1.21 (0.05) | 1038 (8) -> 1045 (8) | 10 -> 5 of 300 |
| MEDIUM | 100 | 0.06 (0.02) -> 0.02 (0.01) | 0.31 (0.07) -> 0.21 (0.06) | 3.03 (0.09) -> 3.01 (0.09) | 1230 (21) -> 1235 (21) | 19 -> 20 of 300 |

On TREASURE the turtle's hill lies in reach of every bot and each of the three went into its base about three times a match; with the rule none does (one trip in 50 bot-matches). The ants of the bots that are blown up in the turtle's yard are as many as before: this rule does not change where they walk.

## Switches and measuring

All four are in the Hard plan only (`LevelPlan::opportunity`, `reach_fights`, `idle_hunt`, `chore_worth`; `opportunity_reach` 8, `opportunity_jobs` 2, `opportunity_ticks` 240, `opportunity_still` 20, `opportunity_skip_guard`, `opportunity_harvest_d` 2). `bot_arena --tune kill=0` takes all four out and plays the old Hard player exactly (the same state hash in every match); `opp=0`, `reach=0`, `idlehunt=0`, `worth=0` take one out; `oppreach=`, `oppjobs=`, `oppticks=`, `oppstill=`, `oppguard=`, `oppharv=` set the numbers. Medium and Easy are not changed (they still send orders at enemies across water; that is the same fault, left for a later change).

`bot_watch` (`tools/bot_watch.cpp`, target `bot_watch`) plays matches like `bot_arena`, and counts per seat and per minute what the bots do badly, from outside the state: idle ants and how long, ants that do not get anywhere, orders the engine refuses, enemy ants that stood where a blow from the far side would have killed them while a free ant of the seat stood near (and whether the seat took the chance), carriers that were passed, walls and rings of fire, bombs that hit the seat's own ants. It reads the engine and the orders and never writes back. `--out FILE` writes a JSON report (the counts of every seat, per minute, and the worst scenes); `--replay FILE` counts the same over recordings. A match under watch ends on the same state hash as the same match in `bot_arena`. It also counts, for every seat, the numbers by which a player is compared with people (hitting back and helping, deaths that could have walked home, heals, Fire Ants, mines round the own hill, focus on a victim), and seats the bench bot `gang`: [BOT_PEOPLE.md](BOT_PEOPLE.md).

## What was measured

Every figure is from `bot_watch` or `bot_arena` matches of 8 minutes, the new Hard player against the old one (`kill=0`) in the same matches: four players, two of each, every arrangement of the seats (or two players, both orders); a figure is a difference with its 95 percent interval (normal approximation over the matches). "Win" is the share of matches in which the seat scores more than the best seat of the other kind (ties count a half). Maps: the 6 shipped ones; 9 community maps that the work was tuned on (water, fire walls, bombs: !!!BLOCKADES!!!, !-Midz-!, !SpY VS sPy!, Alley Ants, Ants World, Arena101, Bomb Island, CRAZY 1, ant party); 10 community maps that were first held out (!!!Bridges v4!!!, A&E, ALL IN ONE !, ANT ZONE, ARMIES OF FATE, Acid Blood, Ant's Island, Bunker, aNtS_oF_1998, anthology grid; the first version lost on them, and the rules above were made for it, so they are no longer held out); and all 534 maps of the library, one seed each, two arrangements (the last check, on maps that were mostly never looked at).

| | matches | kills per seat (old, new) | enemy ants drowned in a water chance, last damage by this seat (old, new) | win, new minus old | points, new minus old |
|---|---|---|---|---|---|
| four players, shipped maps | 576 | 1.77, 1.81 | 0.18, 0.21 | -0.007 +-0.059 | +3 +-25 |
| four players, 9 tuned maps | 648 | 2.75, 3.62 | 0.38, 0.84 | +0.034 +-0.043 | +3 +-37 |
| four players, 10 first held-out maps | 960 | 5.10, 7.12 | 1.55, 2.66 | -0.015 +-0.034 | -32 +-49 |
| four players, 534 maps of the library (the final code) | 1,068 | 2.82, 3.24 | 0.32, 0.52 | +0.013 +-0.019 | (see the text) |
| two players, shipped maps | 360 | 1.05, 0.98 | 0.08, 0.08 | -0.008 +-0.103 | -34 +-70 |
| two players, 9 tuned maps | 432 | 1.85, 2.14 | 0.19, 0.37 | +0.134 +-0.070 | +338 +-139 |
| two players, 10 first held-out maps | 320 | 4.39, 5.64 | 1.23, 1.68 | +0.062 +-0.081 | +100 +-224 |

So the player kills more (a seventh more over the library in the last run, a quarter in the review's, a third to two fifths more on the community maps), more of the enemy ants that drown do so by its last blow, and it loses fewer ants of its own (3.44 to 3.34 per seat over the library). The column "drowned" counts the enemy ants that drowned in a water chance (an enemy beside water within reach of a free ant, for a second) with this seat's team as the last to damage them; it does not say that the quick kill did it (a Combat Ant's punch or a bomb can do it too). **It is not a stronger player:** the points do not change where the whole field plays (four players) and a duel is level too. An independent run of the review with other seeds (4,404 four-player matches over the shipped maps, 20 fresh community maps, the library and the worst maps) found the win share at 0.506 +-0.011 (level), kills +24 percent (3.06 to 3.81 per seat), refused orders 22.8 to 13.2 percent, duels 49.2 of 100 on the shipped maps and 47.8 of 100 on 20 fresh community maps. The +0.134 of the table's duel row is the nine maps the work was tuned on (!!!BLOCKADES!!! and Arena101 carry it), and does not hold on fresh ones. Over the library the points of a few maps with huge negative scores decide the mean, so the win share is the figure: 147 maps went better, 160 worse and 38 equal by points, among the 345 where anyone scores. Against the Medium player the new Hard wins as the old one did: in a duel 0.583 +-0.103 (old 0.567) on the shipped maps, 0.176 +-0.099 (old 0.157) on the tuned maps, 0.308 +-0.084 (old 0.325) on the others.

**Maps that got worse.** Acid Blood (four hills on one island, two ants each, food only over the water): in the first version the new Hard scored 115 against 221 for the old one in the same matches (192 seats), and the review found the same (128 to 69). The only cause was the quick kill. Each seat has two ants, which the island play needs as crews, and the free one went from kill to kill (one seat started 6 jobs in 2,500 ticks on the same target) instead of waiting for the work. Now the last ants of a seat do not go for a kill while food lies beyond water, behind a gate or a field (`far_work`), and Acid Blood plays as the old Hard does (nobody banks anything in either: 0 points for all four seats). A few thief maps lean slightly worse in the review (DEATHTREZ -0.125 +-0.10 and -0.056 +-0.061, Differetini, Fuels war), not provably.

The first version of the quick kill (every still enemy, 8 tiles, any Combat Ant) lost: -0.077 +-0.034 on the first held-out maps and -0.027 +-0.020 over the library. The lesson is in the numbers of its jobs: of 4,600 jobs 7 in 10 ended with no blow because the enemy had only stopped for a moment, and a fifth of the ants taken for them were the economy's. The three rules of "A trip has to be worth it" and the hunting rule for food beyond water made it break even; the points of the pairs are noise at this size (a map is +-150 points with 100 matches), so nothing finer than that is claimed.

What did not improve: the share of ant time spent standing idle did not fall, it rose (37 percent to 47 percent over the library). The orders that the engine refused fell from 30.7 to 19.1 percent of all orders, the ants in the can't-go clip from 4.5 to 2.7 percent of their time and the stuck ants from 6.3 to 4.2 percent, but an ant that is no longer sent to an enemy across the water stands. Of the idle time, nearly all is on maps where nothing can be done (no food that the ants reach, no enemy they can walk to, or the last ants of a decided match): the idle time with a pile that the hill reaches on foot is 2.6 percent (old) and 3.1 percent (new) of the ant time, 63 maps over 5 percent in both, mostly maps with a pond or a gate. Those are the work of the island play and the gate goals ([BOT_GATES.md](BOT_GATES.md)), not of this change.

Two more things that did not change much. An enemy that stands beside the water within three tiles of a free own ant is still left alone about 7 times in 10 (the review: attacked 29 of 100 on the shipped maps, 27 before; 28 of 100 over the library, 17 before): the rules of "A trip has to be worth it" are deliberate, because the first version, which went for every chance, lost matches. And the fire walls: Hard builds and puts out as many as before (4.9 to 4.6 built per seat on the shipped maps); only the ring round an enemy gate is skipped when that team has no food to bank, which changes little (the ring is worth about nothing, +9 +-40 points when switched off).
