# Walled maps: the gate goals, the field breach, and POPcOrN

A Medium or Hard computer player plays the **gate goals** on a map whose food its hill cannot walk to at the start (HELLRAISER and its family, POPcOrN): it gets the ant that the way past needs (a Fire Ant for a fire wall, a Bomber for bombs or a rock wall) and does it. The rules of the bombs, the planner and its numbers are in [bomb_gates.md](reverse_engineering/bomb_gates.md#gate-goals-ants_ai-medium-and-hard-walled-maps); the decision and the known limits in [BOTS.md](BOTS.md#walled-maps-the-gate-goals). A pile that lies beyond a field of bombs, a fire wall and water is the **field breach**'s instead (below). This page is about the map's own bombs, the field breach, what was measured, and POPcOrN in a match: a brief for whoever works on it next.

## The map's own bombs are the gate

On a gate map the counters (`BombTask`) leave the bombs of no team (the map's own: no team, 255) alone while the gate goals are live (not in the stall detector's fallback, not exhausted); the gate goals defuse those that are on their way (`LevelPlan::gate_spare_map_bombs`, `bot_arena --tune gatebombs=0` is the bot before). A team's bomb is countered as on any map, and nothing changes on a map that is not a gate map (the gate play is Medium's and Hard's only: Easy is untouched).

Why: on POPcOrN the counters sent workers to set off the map bombs next to the hill (a worker that steps on a bomb is thrown four tiles, into the bomb field: lost) and Bombers to defuse bombs of the field, over and over: most of the bot's orders there were refused, and the ants that the breach needed were walked away or over bombs (the Bomber that had just defused the corridor to the lake was sent to a map bomb on row 10, walked over (16, 10) and was thrown across the field to its death). The rule reads only what every walled map has (whether the bot plays the gate goals there, and the owner of a bomb); there is no map name, size or coordinate in it.

## The map's own bombs, measured

Hard unless said; "before" is the same build with the rule off (`--tune gatebombs=0`), which plays as the bot of v0.12.57.

| Where | Before | With the rule |
|---|---|---|
| POPcOrN, one team alone, Hard (32 seeds x every seat) | 38 of 128 games bank food; 415 points a game; 14,004 of 19,573 orders refused | 47 of 128; **619 points**; 4,705 of 11,336 refused |
| POPcOrN alone, Medium | 32 of 128; 273 points | 44 of 128; **444 points** |
| POPcOrN one against one (16 seeds) | 0 points per seat (2,491 of 2,644 orders refused) | **0 points per seat** (369 of 661 refused) |
| POPcOrN with four bots (8 seeds) | 0 points per seat | **0 points per seat** |
| HELLRAISER alone (16 seeds x every seat) | 552 points, every game banks | the same, to the point |

Against the bot before in the same matches (the share of the bot with the rule; 50 is a tie; every seating):

| Maps | Seats, seeds | Share |
|---|---|---|
| the 84 walled maps of the gated set (41 of the design half, 43 held-out) | 1 + 1, 8 seeds | 52.4 +- 5.2 over the 19 maps where a bot scores; 17 of them play to the same points (FKD Deathmatch 45.0 +- 13.9, PoPcOrN 3 100) |
| the same | 1 + 3, 2 seeds | 49.6 +- 1.3 over 20; 18 the same (FKD Deathmatch 53.6, Maze The Fantasy 37.5 +- 24.5 on 41 against 49 points) |
| the same, Medium | 1 + 1, 4 seeds | 49.4 +- 1.7 over 20; 18 the same |
| Ghetto, Dustbin, !-Midz-!, FKD Deathmatch, ANTS_PARTY | 1 + 1, 96 seeds | 49.96 +- 0.07; four of them to the same points (FKD Deathmatch 49.8 +- 5.2) |
| HELLRAISER and HELLRAISER III | 1 + 1, 32 seeds | the same points (50.00) |

So the rule changes the play on the maps whose own bombs the counters touched (the POPcOrN family, FKD Deathmatch, a few others) and nowhere else; alone on POPcOrN it is worth half again as many points, and it does not make POPcOrN score in a match.

## The field breach

Some maps put their food beyond a **field**: map bombs, a fire wall and water, so that no walk with only one kind of wall open reaches it (the gate analysis calls such a pile Sealed or Mixed). POPcOrN is one: the pile stands on an island in a lake in the middle of a bomb field, between two fire walls. A Medium or Hard bot opens such a pile with **one ant** that does all the work, in an order derived from the map (`FieldTask`, `field_plan.hpp`): it becomes a Bomber, hops into the field with one bomb where the field has bomb-free ground in reach, digs a corridor, hops onto the pile's island from the dug ground, digs there, becomes a Swimmer and bridges the water that the Fire Ant will cross, becomes a Fire Ant and puts the route's fire cell out, and a Swimmer then carries. `bot_arena --tune field=0` is the bot before (the gate goals' shore breach on those piles), and a bot with the switch off plays every match exactly as the bot of v0.12.59 (state hash identical).

**When** (`StandardBot::decide_gate_map`, once, at the first look of a gate map): `plan_field` finds a pile whose route from the hill crosses fire cells or bombs; every kind of work has a source (a way for a Fire Ant to a side of the route's fire, and for a Swimmer to the land beside the water of that way); and an ant of the bot can get the kind of the first work (`field_startable`; on DeJaVoO the Fire power-ups lie behind the fire wall, so no ant gets a Fire Ant, and the gate goals' raid of the pile banks there as before). The piles that are Sealed or Mixed at that look are the breach's for the whole match (`Tactics::field_piles`): the gate goals leave them alone, also after the work has changed their gates, and play every other pile as before.

**The plan** (`plan_field`, a pure function of the grid and the look):

- **The route**: the carriers' way from the hill to a side of the pile, walking and swimming; its bombs are defused and its fire cells put out first. The cheapest pile of the breach; the route and the sources chosen are kept while they hold, so that the work goes on toward the same targets.
- **The carriers' bombs.** The engine gives a carrier the cheapest walk, on which another team's bomb is no obstacle (an ant that steps on one is thrown), and of walks of the same cost the one its search meets first. So every bomb on a walk that costs at most half a step more than the cheapest one between the hill and a side of the pile is work, and so is every bomb round the pile (a power-up that an ant leaves there turns the walk aside). A bomb of the seat's own colour never throws its carriers (it is a wall to them) and is work only where the route needs it.
- **The fire way**: the way of a Fire Ant from its source (a Fire power-up, or a Fire Ant of the bot) to a side of the route's fire cell; it walks through fire walls, and a bomb on it is defused first, its water bridged first. A source whose way has work in a region of the field that none of the route's work is in costs the one ant a hop more. An ant that takes a power-up leaves its own on one of the free land tiles beside it (the engine draws which), and that power-up shuts a way over the tile: from a Fire power-up with n such tiles a way out over one of them counts `kDropShut / n` more, so it leaves over water (bridged) where it can.
- **The swim way**: the way of a Swimmer to the land beside the fire way's water.
- **The work**, in this order: the bombs (a Bomber), the bridges (a Swimmer), the fire (a Fire Ant), and for a route that swims a Swimmer to carry. A Swimmer cannot defuse; a Fire Ant can neither defuse nor swim.

**The steps** (`next_field_step`). The engine's walks go over bombs, and it works an ability from the first free side of the target nearest the ant (N, S, W, E; a bomb counts as free), so in the field the ant only ever steps to a tile next to it and works a target from the side it stands on; it walks several tiles only over ground with no bomb on or next to the way. Where no dig reaches the work, a Bomber hops with a bomb of its own: the fewest bombs first (a chain over map bombs stops at a dud one time in five and strands the ant), landing where a dig reaches the work; it leaves the hill's side with at least 8 hit points (it heals first). An ant that took a power-up never takes another one whose tile has a single free land tile beside it and nothing else its new kind leaves over: the power-up it leaves would shut it in (an ant of the level's kind leaves nothing and may).

**The task** (`FieldTask`): one ant, the one that gets the work's kind soonest (`field_ant_cost`; the hill's side first), held at the rank of the gate goals; one order at a time, to an ant that stands. An ant whose order the world does not carry out (the same order four times with nothing of the ant changed, or 45 seconds with no change) is given up and rests two minutes; after four ants given up the task waits until the bot's ants or the power-ups change. The bombs of the seat's own colour that lay on the map at the task's first look are work like any other (many levels give the map's bombs to team 0); a bomb planted since, also on a tile where one of them was, is the ant's own (a hop's) and a wall.

**The carriers** are the ferry's Swimmers (`FerryTask`). On these maps the ferry also sends again a Swimmer whose loop ended away from the pile: its walk back found no way, another ant stood in the one-tile corridor. Elsewhere the ferry is unchanged.

### Measured

Hard unless said; "before" is the bot with the switch off (`--tune field=0`), which plays every match of the measurements exactly as main did (188 of 188 matches with main's state hash: the 84 gated maps and the five maps below, in self-play).

| Where | Before | With the field breach |
|---|---|---|
| POPcOrN alone (96 seeds x every seat) | 137 of 384 games bank food; 580 points a game | **384 of 384; 2,317 points** |
| POPcOrN alone, Medium (96 seeds x every seat) | POP_MED_OFF | **POP_MED_ON** |
| POPcOrN one against one (16 seeds) | 0 points per seat (0 of 32 seat-games bank) | **1,139 points per seat** (26 of 32 bank; seat 0 831, seat 1 1,447) |
| POPcOrN with four bots (8 seeds) | 0 points per seat | **573 points per seat** (18 of 32 bank; seat 2 1,425, seat 0 662, seats 1 and 3 75 and 131) |

The field family: the 50 maps of the library where an early version of the rule played for some seat (the list was fixed early in the work, before the last rules and constants; the final rule plays on 40 of them and on no other map of the 540), split by the fixed design / held-out halves. A Hard bot alone, 8 seeds x every seat:

| Half | Points a game (mean of the maps), before | with | Seat-games that bank, before | with | Maps better / worse |
|---|---|---|---|---|---|
| design (25 maps, POPcOrN among them) | 37 | **358** | 45 of 800 | **191 of 800** | 12 / 0 |
| held-out (25 maps) | 3,326 | **4,056** | 122 of 800 | **394 of 800** | 14 / 1 (pop xx: 4 to 0) |

Against the bot before in the same matches (the share of the field breach's points; 50 is a tie; every seating):

| Maps | Seats, seeds | Share |
|---|---|---|
| the 84 walled maps of the gated set (41 design, 43 held-out) | 1 + 1, 8 seeds | 77.2 +- 7.8 over the 40 maps where a bot scores; 19 better, 1 worse (Crispy ownage 22.2 +- 20.0, 23 against 71 points a seat); design 87.0 +- 10.9, held-out 70.7 +- 10.1 |
| the same | 1 + 3, 2 seeds | FFA13_LINE |
| the same, Medium | 1 + 1, 4 seeds | MED_LINE |
| Ghetto, Dustbin, !-Midz-!, FKD Deathmatch, ANTS_PARTY | 1 + 1, 96 seeds | Dustbin 80.4 +- 4.7, !-Midz-! 62.8 +- 3.3; the other three to the same points |
| the HELLRAISER family (6 maps) | 1 + 1, 32 seeds | the same points (50.00) |

**Limits.** On 14 of the 40 maps where it plays, the breach does not bank alone yet (ARMIES OF FATE, BiTe ThE DusT 2, Bomb Island, Burnt And Crispy, CrUnChY BiTe 2, Crispy death, Die Another Day, Element, Obstacle Course, PoP it up PoPcOrN, SpIcY BiTe 4, SpIcY BiTe 5, SpIcY BiTe ReVeNgE, pop xx); 13 of them scored nothing before either. On pop xx the start ants of seat 0 stand in the half without the work: the breach takes the pile and fails there, where the gate goals' raid of it banked one bite of 30 points in half the games (starting a breach only with an ant on the hill's side keeps that bite, but cost far more on ThE ReVeNgE, CrUnChY BiTe and !-Midz-!, whose start ants stand away from their hills too). In a duel on Crispy ownage the opened wall lets the other team's thieves through (23 against 71 points a seat). A bridge lasts 3,600 ticks: the walkers that carry over it before it falls in are the plain economy's; after that only Swimmers carry.

## POPcOrN in a match

**The map.** 31 x 31. The hills of teams 0 and 2 are in the north half (rows 0 .. 8), those of teams 1 and 3 in the south half (rows 22 .. 30). Fire walls run across rows 9 and 21; between them lies a field of 249 map bombs (rows 10 .. 20) with a lake in the middle (row 14), and the only food, 50 units of 50 points, at (15, 15) in the lake. Each half has Bomber, Thief and Combat power-ups but no Fire power-up; the field has 5 Swimmer, 3 Fire and 2 Bomber power-ups. The start ants are Combat Ants.

**With the field breach** a bot alone banks in every game, and in a match the bots score (the table above): about 1,140 points a seat one against one, 570 with four bots. The seats are not equal. **The start ants are dealt across both halves**: teams 0 and 1 have one start ant in their own half and two in the other, teams 2 and 3 two at home and one away, and in a match every half holds Combat Ants of two to four teams, which fight at once (the home ants attack the strangers first). A breach needs an ant of the own half for its first Bomber, so the seats with one home ant start later or not at all: with four bots seat 2 banks in 7 of 8 games and seat 1 in 2. The breach ant is held at the rank of the gate goals, above the fights, but a fight can still kill it.

**The breach before** (`--tune field=0`: the gate goals' shore breach) hopped a Bomber onto the field's Fire power-up over two bombs, bounced home over map bombs, put out the fire cell at the shore end of the walk to the lake, defused that walk's bombs with a home Bomber again, and sent a Swimmer reached by another hop: alone it got through in about a third of the games, and in a match never (the home ant died in the fights before it had its Bomber, chains stopped at duds and stranded ants, a flight onto a power-up took nothing, and the wall keeper and the power-up trips took the time).

**Tried and left out** (Hard, against the bot without it):

- **The carriers' walks predicted with the engine's own search** (`sim::PathSearch`, from every tile a carrier leaves the hill by to every cell of the pile) instead of every walk within half a step of the cheapest: the walk the engine gives depends on the power-ups an ant leaves and the bridges of the moment, so the bombs to defuse changed while the breach went on; alone on POPcOrN 2,086 points against 2,214.
- **A landing that a dig from reaches all the island's work** (a hop is valued one hop dearer when it leaves work out): no change on POPcOrN (2,230 against 2,214), +12 seat-games of 800 on the design half, -2 on the held-out half; left out.
- **A breach started only by an ant on the hill's side**, or only by one that, as the first work's kind, has a step toward the work: each kept pop xx's one bite of 30 points, and each lost the breach on maps whose start ants stand away from their hills (ThE ReVeNgE 2,422 to 0, CrUnChY BiTe 1,262 to 0, !-Midz-! 1,575 to 272, TeAMs OWn 580 to 44).

- **The next goal at once** (an explicit claim of the gate goal on its ant: when a goal is done, plan the next one in the same look and take its ant before the other tasks look, instead of 20 ticks later). Alone on POPcOrN it lifted a Hard bot from 415 to 464 points, but with the rule above it added nothing (606 against 619), and it cost the vault raids of the HELLRAISER family in matches: duels 43.6 +- 9.2 percent of the share on HELLRAISER and 26.0 +- 16.7 on HELLRAISER III (32 seeds x 2 seatings), and 30.3 +- 24.7 on HELLRAISER with four seats. A likely cause, not yet checked: the next raid takes the Bomber at once with the hit points for the way in but not for the way back.
- **No job for a target that no walk reaches** (the counters, the mines, the wall keeper; the old work-in-progress `reach`): it ended the refused orders but cost Ghetto 19 points of share in duels (31.0 +- 14.9 percent, 8 seeds x 2 seatings), ANTS_PARTY and FKD Deathmatch about 5 each, and was no better on POPcOrN than the rule above.
- **A stranded Bomber that defuses its way to the target**: a first version changed nothing (the cases it was meant for ended before it, in a walk home to heal that no walk could make); not finished.
- **Holding the walls and the own side's power-up trips back for the whole breach** instead of the first 2,400 ticks: one seat-match of 32 more (noise).

**Next** (in this order): keep the home ant of a breach out of fights it does not have to take (and let the ants of the other half fight or harass there); the 14 maps of the field family where the breach plays and banks nothing; on Crispy ownage keep the wall shut against the other team's thieves once the food is home; for the HELLRAISER family, a claim of the gate goal on its ant that checks the hit points for the whole raid. Measure with `bot_arena --map Community-Maps/POPcOrN.lvl` one against one and with four bots, as well as alone (`--seat N=standard:hard` for one seat), and on the HELLRAISER family.
