# Idle ants, bridges, fire hats and thieves: the idle batch of the computer players

Part of [Bots (computer players)](BOTS.md). The tactics are in [BOT_TACTICS.md](BOT_TACTICS.md), the water maps in [BOT_ISLANDS.md](BOT_ISLANDS.md), the end of the food in [BOT_GONE.md](BOT_GONE.md), the ants that stand nowhere in [BOT_UNSTUCK.md](BOT_UNSTUCK.md). Source: `LevelPlan` in `include/ants_ai/tactics.hpp` (the fields below), `bridge_headon.cpp`, `contact_task.cpp`, `idle_jobs_task.cpp`, `thief_guard.cpp`, `island_bridge.cpp`, `island_link.cpp`, `island_swarm.cpp`, `standard_tasks_raid.cpp`, `standard_tasks_powerup.cpp`; tests AI58 (`tests/test_ai/test_ai_idle.cpp`), mutants `tests/data/bot_idle_mutants.json`. `bot_arena --tune idlebatch=0` is the bot as it was before the batch (the same match, hash for hash, as the bot without it: checked on every map and level); one key a rule is listed with it.

## What was found

A person played one match on ISLANDS against three Hard bots and won with 13 steals made by two Thieves (+650 for him, 650 off the bots; without them he would have been last). The recording was played again on the engine, tick by tick, and every ant that stood still for two seconds or more was classified.

- **The bots stood idle for half their ant time** (10 seconds or more: 53, 40 and 49 percent; the person 48), and for 93 to 97 percent of it once the last food was gone. A third of every bot's ant time was **no order and nothing planned**: Swimmers after the last pile, Bombers after their mines, a worker that no walk leads anywhere. The rest: ants that could not go (a refused order again and again), ants posted at a flower strip for a drop, and one pair face to face on a bridge.
- **All 13 steals were unopposed.** No blow ever landed on a Thief, no fire wall stood near a raid tile, no bot ant stood in front of a hole. The bots had no Thief of their own: the Thief of a flower strip was skipped by the pick-up task and the swarm took Swimmers only. No bot took a Fire hat before minute 9 (a Fire hat on ISLANDS lies on the ring, 3 to 6 water tiles from a hill island, and `fire_home_only` refused any hat from which no walk leads home), and the bots dug 8, 3 and 14 bridge tiles against the person's 129.
- **Two ants of one bot stood face to face on a two-tile bridge for 113 seconds**: the engine's rules are the original's (an ant that finds its next tile held by a moving ant waits, one held by a standing ant plans a way round with a bump, an ant of another team in the way is no way at all, and there is no swap, no yield and no push), the economy keeps one ant to a bridge, and nothing covered the heal walk of a hurt worker and the swarm's Bomber.
- **A worker stood 82 seconds beside a Bomber of the person's and 52 beside his Thief and struck neither**: the offence of the bots is made of groups (the assault wants two free ants, the hunt a wounded enemy, the answer a blow).

## The rules

All are plan switches that ship on at every level but one (`LevelPlan`, `plan_for`; rule 2 is off by default); the ones that need Hard's island machinery (the swarm, the flower link and the bridge planner: `island_flower_link`) do nothing below Hard and nothing off the island maps. Each has a key of `bot_arena --tune` (`idlebatch=0` is all of them off). Everything is an ordinary command of a person (a plain move, an attack, a special order onto a water tile or a hill), through the controller's budget and reaction time, from what the seat's team sees.

1. **The Thief of the endgame** (`island_endraid`, `endraid`; `endraid_from` 1,200, `endraid_thieves` 2, `endraid_tiles` 12, `endraid_walk` 500, `endraid_min_life` 500, `endraid_keep_swimmers` 3). From the first minute an own Thief is wanted (`Tactics::endraid`; later the food gone counts as well, and `wants[Thief]`): an idle Swimmer that the ferry can spare (the bot keeps three) swims to the strip when a Thief drop lies on it, and the spare ants that wait at a strip take it (`PowerUpTask`, `SwarmTask`). A Thief that stands away from the hill gets its chain (`BridgeJob::Kind::Raid`, dug by the Swimmer of the flower link, which goes first before a link for Swimmers): the way from its island to the own hill's island (the loot is banked there, and the Thief waits there) and ONE channel from the own hill's island to that of the richest enemy hill whose hole is open (3 or 4 tiles: the neighbor hills), never over the island of another enemy hill and never when the target is raidable already by a walk of at most `endraid_walk` ticks. The Thief walks home first when a walk leads there (`RaidTask`), and the raid task raids over the chain: only when the way home from the raid side exists as the map is now and every bridge on the way there and back outlives the round trip by `endraid_min_life` ticks (`Tactics::bridge_life`, published by the island task). A Thief that has the loot and stands where no walk leads home gets the chain home like a hurt ant (a Return job over the pool without the islands of other enemy hills; the engine walks it home when the way stands). The ants that wait at a strip for a drop never wait on a bridge piece (they would shut it for the Thieves that come off it). Fairness: the drops, the bridges, the scores and the holes are on every screen.
2. **The answer to a Thief at the hole** (`thief_answer`, `tanswer`; **off by default**: the arena shows no gain, and a loss that it cannot rule out on TREASURE, see "Measured"; `bot_arena --tune tanswer=1` plays it, and the switch is retired or removed within two releases unless a bench Thief that raids like a person shows a gain). The meeting of the thief batch ([BOT_TACTICS.md](BOT_TACTICS.md#thieves-the-answer-to-a-raid): blockers on the tiles in front of the hole, strikers at the posts, a blow at a Thief that comes within three tiles) runs at every level in its answer mode when it is on: only ants that stand IDLE are taken (not a worker on its way to a pile). With the Fire Ant of rule 3 the walls of the hole come first, as before (a wall is lit on no tile that an ant holds).
3. **Fire hats** (`fire_hats`, `firehats`; `fire_hat_tiles` 6, `fire_hat_wait` 60). `fire_home_only` no longer refuses a Fire hat on the ring while the bot has Swimmers to spare for a chain and time to use the Fire Ant. A Fire Ant that stands away from the hill with no walk home gets the chain to the hill's island (`BridgeJob::Kind::Home`, at most 6 tiles) and is the planner's until it is home; then the keeper of the walls (`WallTask`) and the counters of fire put the walls of the hole up and the enemy's out.
4. **More bridges** (`bridge_more`, `bridgemore`; `bridge_more_tiles` 5, `bridge_more_idle` 120). While the endgame Thief is wanted or the food is gone the bridge planner digs for more ants: the chains of the older jobs may be 5 tiles, an ant waits 120 ticks for its bridge (300), one Swimmer is enough to dig, and every idle ant that a walk joins to the hill now walks home (a hurt one into the hill, a whole one to the tile where the ants of the hill wait).
5. **Head-on on a bridge** (`bridge_headon`, `headon`). Two ants (any team, one of them the seat's own) on neighboring finished bridge tiles that have not moved for 60 ticks are a jam: the own ant that can step back to land soonest (the search leaves the other ant's tile out) is clicked to that land tile, held there for 200 ticks and given back (`BridgeHeadOnTask`, rank 5: it takes from every task below a fight); an ant that shows the can't-go clip gets a volley of clicks, which is the way a person hits the one tick that takes an order ([BOT_UNSTUCK.md](BOT_UNSTUCK.md)). A Swimmer is never a member (it swims past). The same task clears the feet of a bridge: an own ant (no Swimmer) that has stood idle for 100 ticks on the land tile at the foot of a finished bridge piece, with an ant of any team that cannot go within 8 tiles (in the can't-go clip at three looks, or standing on a bridge tile beside it), is sent to the nearest free land tile that is no foot, and held there for 200 ticks: the landing of a bridge is one tile, and an ant on it is no way at all for the engine. The search for the tile is made over the 8-tile box only.
6. **A blow for the ant beside the enemy** (`contact_strike`, `contact`). A free ant (no task, or one of rank 2: the economy, the guard, the ferry; not a Fire Ant, a Thief or a Swimmer that is no worker: the engine refuses the blows of a swimming ant) with 6 or more hit points that stands idle beside an enemy ant that has stood still for 40 ticks (a Thief at once), that has no more hit points, is no Combat Ant and does not stand within 6 tiles of its own hill, strikes it, one attack order a blow, at most 10 blows and 300 ticks; not where an enemy Combat Ant or two enemy ants drawn fighting lately stand within 5 tiles, and not in the last minute of a bot that leads (`ContactTask`).
7. **A job for the ants that nothing holds** (`idle_jobs`, `idlejobs`; `idle_after` 400, `idle_heal_hp` 8, `idle_posts` 3). An ant that has stood idle with empty hands and no task for 400 ticks gets one of two jobs (`IdleJobsTask`, rank 2: any task of a higher rank takes the ant): a hurt one (8 hit points or fewer) walks into the own hill when a walk leads there, and, with the food gone, or an enemy Thief seen within the last 3,600 ticks and no pile that can be reached (with food to carry the ants are the economy's, which cannot take a holder back), up to three of the idle ants stand on the tiles in front of the own thief hole that no wall, bomb or ant holds and shut it (a raid order whose three tiles are held by standing ants ends in "Can't go there.", whoever gave it, and a bot's raid task leaves such a hole alone). Not while a Fire Ant that can walk home keeps the walls, and Swimmers stay out of it while the bot has three or fewer.

## Measured

**How.** `bot_arena` with `--rotate`: two seats play the plan that ships and two the same bot with a switch off (`--seat N=standard+styled=1,KEY=0:LEVEL`; `idlebatch=0` is all seven), in every one of the six ways to seat them (216 matches for 36 seeds), the whole match (every match of the last runs is replayed and checked). "Idle %" is the share of the ant time that ants stood 10 seconds or more with nothing to do (the whole match, and after the last food was taken); "steals" are the steals that a seat made (with their points); "matches won" counts the matches that a seat of the group won outright. The last column is the mean over the seeds of the score of the seats with the rule minus that of the seats without it, with twice its standard error over the seeds: a gain that the column shows is the one that the seeds show. Reports: `bot_arena --out` (format 9 has the idle counters and the counters of each rule).

*The whole batch against none, ISLANDS (36 seeds for Hard, 12 for Medium and Easy; the answer to a Thief is off in the batch):*

| | | score a seat | idle % | idle % after the last food | steals a seat (points) | bridge tiles a seat | Fire hats a seat | matches won | score gain, on minus off (± 2 SE) |
|---|---|---|---|---|---|---|---|---|---|
| Hard | batch | 1486 | 43 | 75 | 4.23 (212) | 17.5 | 0.94 | 196 | +176 ± 28 |
| | none | 1310 | 52 | 91 | 0.00 (0) | 6.0 | 0.23 | 18 | |
| Medium | batch | 1402 | 61 | 86 | 0.00 (0) | 0.0 | 2.00 | 36 | +1 ± 1 |
| | none | 1402 | 62 | 87 | 0.00 (0) | 0.0 | 2.00 | 36 | |
| Easy | batch | 1044 | 67 | 74 | 0.00 (0) | 0.0 | 2.00 | 33 | -1 ± 2 |
| | none | 1045 | 67 | 74 | 0.00 (0) | 0.0 | 2.00 | 33 | |

*One rule off and the rest on, ISLANDS, Hard, 36 seeds (the "on" seats are the same in every table: all of the batch that ships):*

| | | score a seat | idle % | idle % after the last food | steals a seat (points) | bridge tiles a seat | Fire hats a seat | matches won | score gain, on minus off (± 2 SE) |
|---|---|---|---|---|---|---|---|---|---|
| The Thief of the endgame | all on | 1478 | 43 | 75 | 4.12 (206) | 17.1 | 0.82 | 176 | +161 ± 34 |
| | this rule off | 1317 | 48 | 82 | 0.04 (2) | 8.2 | 0.91 | 39 | |
| Jobs for idle ants | all on | 1394 | 45 | 77 | 2.11 (106) | 13.4 | 0.97 | 102 | -8 ± 15 |
| | this rule off | 1402 | 46 | 80 | 2.08 (104) | 13.5 | 1.03 | 108 | |
| A blow beside the enemy | all on | 1398 | 45 | 77 | 2.12 (106) | 13.4 | 0.94 | 105 | -1 ± 3 |
| | this rule off | 1399 | 45 | 78 | 2.17 (108) | 13.6 | 0.97 | 105 | |
| Head-on on a bridge | all on | 1396 | 45 | 77 | 2.06 (103) | 13.7 | 0.97 | 103 | -5 ± 16 |
| | this rule off | 1401 | 45 | 78 | 2.13 (106) | 13.5 | 0.96 | 106 | |
| Fire hats | all on | 1399 | 45 | 77 | 2.17 (109) | 13.6 | 0.97 | 107 | -2 ± 14 |
| | this rule off | 1400 | 46 | 81 | 2.28 (114) | 12.6 | 0.54 | 108 | |
| More bridges | all on | 1396 | 45 | 77 | 2.11 (106) | 13.6 | 0.94 | 109 | -5 ± 5 |
| | this rule off | 1401 | 45 | 78 | 2.09 (104) | 13.3 | 0.88 | 107 | |

*The answer to a Thief (rule 2, off by default) played against off, the rest of the batch on in both:*

| | | score a seat | idle % | idle % after the last food | steals a seat (points) | bridge tiles a seat | Fire hats a seat | matches won | score gain, on minus off (± 2 SE) |
|---|---|---|---|---|---|---|---|---|---|
| ISLANDS | answer on | 1396 | 45 | 77 | 2.00 (100) | 13.5 | 0.93 | 105 | -4 ± 12 |
| | answer off | 1401 | 45 | 77 | 2.13 (106) | 13.5 | 0.97 | 105 | |
| ISLANDS, wider meeting (tgrad 24, tgreach 16) | answer on | 1394 | 45 | 76 | 2.03 (101) | 13.6 | 0.98 | 105 | -8 ± 19 |
| | answer off | 1402 | 45 | 77 | 2.15 (107) | 13.5 | 0.97 | 105 | |
| TREASURE | answer on | 2028 | 0 | 17 | 2.82 (139) | 0.0 | 1.06 | 99 | -23 ± 69 |
| | answer off | 2052 | 0 | 16 | 2.92 (144) | 0.0 | 1.07 | 117 | |

*The other maps (Hard, 8 seeds; HELLRAISER 48):*

| | | score a seat | idle % | idle % after the last food | steals a seat (points) | bridge tiles a seat | Fire hats a seat | matches won | score gain, on minus off (± 2 SE) |
|---|---|---|---|---|---|---|---|---|---|
| TINY | batch | 1188 | 1 | 4 | 0.00 (0) | 0.0 | 0.00 | 22 | +4 ± 14 |
| | none | 1184 | 2 | 6 | 0.00 (0) | 0.0 | 0.00 | 26 | |
| SMALL | batch | 968 | 13 | 49 | 0.00 (0) | 0.0 | 2.17 | 24 | +1 ± 18 |
| | none | 967 | 14 | 59 | 0.00 (0) | 0.0 | 2.19 | 24 | |
| MEDIUM | batch | 1078 | 1 | 15 | 2.57 (129) | 0.0 | 0.84 | 22 | -12 ± 12 |
| | none | 1090 | 1 | 22 | 2.65 (132) | 0.0 | 0.85 | 26 | |
| GAUNTLET | batch | 278 | 1 | 9 | 4.92 (231) | 0.0 | 0.93 | 23 | -4 ± 21 |
| | none | 282 | 1 | 9 | 5.06 (239) | 0.0 | 0.99 | 24 | |
| TREASURE | batch | 2031 | 0 | 16 | 3.71 (185) | 0.0 | 1.01 | 26 | +19 ± 43 |
| | none | 2012 | 0 | 19 | 3.69 (184) | 0.0 | 1.02 | 22 | |
| POPcOrN | batch | 572 | 36 | 42 | 2.66 (133) | 1.0 | 1.34 | 22 | +10 ± 129 |
| | none | 562 | 47 | 81 | 1.16 (58) | 1.0 | 1.36 | 23 | |
| HELLRAISER | batch | 179 | 34 | 39 | 0.00 (0) | 0.0 | 0.01 | 123 | -3 ± 7 |
| | none | 182 | 35 | 40 | 0.00 (0) | 0.0 | 0.01 | 126 | |
| Bombz_Away | batch | 545 | 1 | 10 | 0.00 (0) | 0.0 | 0.00 | 24 | +36 ± 24 |
| | none | 508 | 2 | 22 | 0.00 (0) | 0.0 | 0.00 | 24 | |
| !TreasureIsland | batch | 0 | 90 | 90 | 0.00 (0) | 0.0 | 0.00 | 0 | +0 ± 0 |
| | none | 0 | 99 | 99 | 0.00 (0) | 0.0 | 0.00 | 0 | |

*Medium and Easy (6 seeds):*

| | | score a seat | idle % | idle % after the last food | steals a seat (points) | bridge tiles a seat | Fire hats a seat | matches won | score gain, on minus off (± 2 SE) |
|---|---|---|---|---|---|---|---|---|---|
| SMALL Medium | batch | 969 | 13 | 49 | 0.00 (0) | 0.0 | 1.29 | 17 | +5 ± 21 |
| | none | 964 | 14 | 44 | 0.00 (0) | 0.0 | 1.42 | 15 | |
| TREASURE Medium | batch | 2081 | 0 | 13 | 3.29 (163) | 0.0 | 1.00 | 18 | +3 ± 15 |
| | none | 2078 | 0 | 10 | 3.36 (167) | 0.0 | 1.00 | 18 | |
| MEDIUM Medium | batch | 1173 | 1 | 8 | 0.81 (40) | 0.0 | 0.24 | 16 | +4 ± 33 |
| | none | 1169 | 1 | 9 | 0.78 (39) | 0.0 | 0.17 | 20 | |
| SMALL Easy | batch | 938 | 8 | 33 | 0.00 (0) | 0.0 | 0.06 | 18 | +6 ± 19 |
| | none | 932 | 11 | 37 | 0.00 (0) | 0.0 | 0.06 | 17 | |
| TREASURE Easy | batch | 2214 | 1 | 11 | 0.22 (11) | 0.0 | 0.11 | 18 | -5 ± 15 |
| | none | 2220 | 2 | 14 | 0.15 (8) | 0.0 | 0.12 | 18 | |
| MEDIUM Easy | batch | 1227 | 2 | 12 | 0.03 (1) | 0.0 | 0.03 | 18 | +7 ± 9 |
| | none | 1220 | 3 | 17 | 0.00 (0) | 0.0 | 0.00 | 18 | |

*What it costs in orders and in ants that stand nowhere (ISLANDS, Hard, with the maps that have Thieves without the batch's island play for comparison; [BOT_UNSTUCK.md](BOT_UNSTUCK.md) has the two counters):*

| | | orders a seat | refused per 1,000 orders | stuck ants a seat and match | in the can't-go clip | carrying with no order |
|---|---|---|---|---|---|---|
| ISLANDS Hard | batch | 543 | 46.1 | 0.27 | 0.12 | 0.12 |
|  | none | 471 | 13.8 | 0.04 | 0.01 | 0.02 |
| TREASURE Hard | batch | 515 | 11.5 | 0.19 | 0.19 | 0.00 |
|  | none | 513 | 11.3 | 0.26 | 0.26 | 0.00 |
| MEDIUM Hard | batch | 284 | 4.3 | 0.19 | 0.09 | 0.00 |
|  | none | 285 | 4.3 | 0.19 | 0.09 | 0.00 |
| POPcOrN Hard | batch | 162 | 87.1 | 0.22 | 0.12 | 0.09 |
|  | none | 132 | 57.5 | 0.20 | 0.09 | 0.10 |

*The counters of the batch in the "batch" seats of the first table, a seat and match:* headon_jams 0.78, headon_backoffs 0.78, contact_jobs 1.13, contact_blows 2.30, holds 0.50, heals 2.03, swarm_thieves 1.09, homed 7.21, raid_tiles 10.81, home_tiles 0.44.

*Four seats of one kind (36 seeds, ISLANDS, Hard, every match replayed): the batch on in all four seats, a mean score of 1,399 a seat; off in all four, 1,400. Every replay played out to the same hashes, in both runs.*

## What the numbers say

- **The gain is the Thief of the endgame, and it is taken from the other seats.** With it the bots steal about four times a match and a seat (212 points) where they stole none, they dig 17.5 bridge tiles a seat where they dug 6, and a seat that plays the batch scores 176 more than one that does not (161 with the Thief of the endgame as the only rule off), and loses 80 points to steals where the others lose 137. When all four seats have it the mean stays where it was (1,399 against 1,400): a steal moves 50 points from one seat to another. The batch is a way to win a match against the bots that lack it (196 of 216 matches against 18), not a larger score for everybody.
- **The other five rules that are on are inside the noise of the score on ISLANDS** (-8 to -1, each within about twice its standard error; the bridge rule, -5 ± 5, is at the edge) and none shows a cost. Each does what it was built for in its own counters: the Fire hats are taken twice as often (0.54 to 0.97 a seat and match, and 1.0 bridge tile a seat more), the bridge rule digs 0.3 more, 2.0 hurt ants a seat walk home, 0.8 head-on jams a seat are undone, 0.5 holds of the thief hole begin. The idle share after the last food falls from 91% to 75% with the batch: 7 points of the 16 with the Thief of the endgame, 4 with the Fire hats and 3 with the idle jobs; what is left stands because standing is the work (the ants at the thief hole, the ferry's Swimmers, the Thief that waits for its chain).
- **The two defences show nothing in the arena, and the answer to a Thief is off.** Played, the answer reads -4 ± 12 on ISLANDS (-8 ± 19 with the wider meeting) and -23 ± 69 on TREASURE, where it also won fewer matches (99 against 117 of 216): no gain anywhere and a cost that 36 seeds cannot rule out where the meeting was found to cost points (docs/BOT_TACTICS.md). It leaves the points raided from a seat where they were (106 with it, 104 without; the holds of the idle jobs: 106 and 109). AI58.3 and AI58.4 show the hill that keeps its 50 points in a world with a Thief that goes for it, so the idle jobs' holds stay on (they cost nothing: -8 ± 15) and the answer is a switch that is off (`tanswer=1` plays it) until a bench Thief that raids like a person shows a gain; it is retired or removed within two releases otherwise. The bots' own Thieves (2.1 steals a seat and match) are too few to be the test (the match that was watched had 13 steals by a person).
- **Medium and Easy do not change** (+1 and -1 on ISLANDS; every row of the small-map table is inside twice its standard error): the rules of the island play need Hard's machinery, and what the other rules do at those levels is too little to show. The other maps do not change either, except where there is a Thief to steal with and the food is gone early: POPcOrN (steals 1.2 to 2.7 a seat, idle after the last food 81% to 42%; +10 ± 129 with 8 seeds) and Bombz Away (idle after the last food 22% to 10%; +36 ± 24 in score, the one other gain that is more than its error). HELLRAISER, with 48 seeds, reads -3 ± 7. On !TreasureIsland nobody banks anything (0 points in both groups; the idle share is 90% against 99%).
- **What it costs.** On ISLANDS the refused orders rise from 14 to 46 in 1,000 and the ants that stood nowhere for 10 seconds from 0.04 to 0.27 a seat and match: of the rise, 0.12 are Thieves in the can't-go clip and 0.12 carriers with no order (0.02 before). That is what Thieves do on the other maps (TREASURE, without the batch: 11 refused in 1,000 and 0.26 stuck ants; POPcOrN: 58 and 0.20), and the orders stay inside the budget of the level; it is not at the level of the bot without the batch. What was done about it: a Thief with loot that no walk takes home gets a chain home, the ants that wait at a strip no longer stand on a piece of a bridge, and an ant that stands idle on the foot of a bridge with an ant that cannot go behind it steps off (rules 1 and 5): stuck ants a seat 0.31 to 0.25, carriers 0.19 to 0.12, over the same 24 seeds; the rest are deadlocks of ants that a higher task holds at the end of a one-tile bridge.
- **What it costs in time, and what was done about it.** Four Hard bots on ISLANDS took 1.46 times the time of the bot without the batch (2,476 against 1,695 ms a match over four seeds on the measuring machine; the review of this change read 1.46 too); after the changes below 1.20 times (1,811 against 1,515 ms), and on !TreasureIsland 1.21 (the review: 1.60). The raid task looks for a raid of an island Thief once in 60 ticks and shares the floods (the walk home, the walk back from a raid tile) among the Thieves of a look, the bridge planner and the idle jobs look at the map's walks once in 20 and 100 ticks, the pick-up task makes its walk mask only when a field is made, and the head-on search floods the 8-tile box only. The server's CPU test has a row for ISLANDS: 5.3 times the idle bots (bound 8; 4.9 to 5.1 without the batch).
- **Where it was found by measuring.** An early build ran the raid task's island mode on the gate maps as well (HELLRAISER's idle share doubled, its raids moved): `Tactics::island_play` ties it to the island maps, and AI58.9 plays HELLRAISER with the island rules on and off to the same match. The blows of the ferry's Swimmers were refused again and again by the engine (a fifth of the batch's refused orders): `ContactTask` leaves them to the ferry. The idle jobs flooded the whole map at every look while a Fire Ant stood on it (three times the time of a match of four Hard bots), and the posts at the thief hole were held for the rest of the match once a Thief had been seen, with food on the map (the economy, of a lower rank, cannot take a holder back): they are held while the food is gone, or a Thief was seen within 3,600 ticks and no pile can be reached.

## Left out

- **The check of the bridges on a raid is the descent of a cost field.** `route_life` (`standard_tasks_raid.cpp`) walks down the cost field from the raid tile to the Thief and takes the least life of the bridge tiles on that walk. The engine's own path can differ (a tie between a new and an older bridge, a diagonal), so the guard of `endraid_min_life` is conservative where the two walks agree and not where they differ. Nothing stronger is known to the bot without running the engine's path search.

- **Hatching a rival** (a wipe-out costs a team min(box, 200) points): on ISLANDS a team has 8 ants on several islands and no Combat hats; 6 ants were lost in 12 minutes in the recorded match. Not within reach, not built.
- **Fire-in of a rival hill** after the food is gone: the rival banks nothing, so it has no value. Not built.
- **A bench bot that raids like the person** for rule 2 alone: the rule is measured against the bots' own raids (the four seats raid each other under rule 1) and in the hand-made world of AI58.3.

## Tests

AI58.1 to AI58.14 in `tests/test_ai/test_ai_idle.cpp`, in hand-made worlds with the real engine and whole matches of ISLANDS (every match replayed). The tests of the older rules play the plan as it was (`plan_as_was`, `without_idle_batch`); the mutants are `tests/data/bot_idle_mutants.json` (`python3 tools/mutate.py tests/data/bot_idle_mutants.json --keep-going --jobs 1`). The server's CPU test (`tests/test_server/test_server_bots.cpp`, S3.71) has a row for four Hard bots on ISLANDS over 100 seconds of play in three rooms, with its own bound (8 times the idle bots; 5.3 with the batch, 4.9 to 5.1 without it, on a quiet host). The raids of the island Thieves look for a walk once in 60 ticks, the walks of the bridge planner and of the idle jobs are looked at once in 20 and 100 ticks, so that the whole batch costs the bots about a fifth more time on ISLANDS (7.2 s against 6.1 s for four seeds of four bots).
