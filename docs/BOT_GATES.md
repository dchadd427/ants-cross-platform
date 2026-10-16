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

**When** (`StandardBot::decide_gate_map`, once, at the first look of a gate map): `plan_field` finds a pile whose route from the hill crosses fire cells or bombs; every kind of work has a source (a way for a Fire Ant to a side of the route's fire, and for a Swimmer to the land beside the water of that way); and an ant of the bot can start it (`field_startable`): it gets the kind of the first work and, as that kind where it gets it, has a step toward the work. (On DeJaVoO the Fire power-ups lie behind the fire wall, on pop xx the start ants stand in the half without the work: there the gate goals' raid of the pile banks, as before.) The piles that are Sealed or Mixed at that look are the breach's for the whole match (`Tactics::field_piles`): the gate goals leave them alone, also after the work has changed their gates, and play every other pile as before.

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

FIELD_MEASURED_PLACEHOLDER

## POPcOrN: why it does not score in a match yet

**The map.** 31 x 31. The hills of teams 0 and 2 are in the north half (rows 0 .. 8), those of teams 1 and 3 in the south half (rows 22 .. 30). Fire walls run across rows 9 and 21; between them lies a field of 249 map bombs (rows 10 .. 20) with a lake in the middle (row 14), and the only food, 50 units of 50 points, at (15, 15) in the lake. Each half has Bomber, Thief and Combat power-ups but no Fire power-up; the field has 5 Swimmer, 3 Fire and 2 Bomber power-ups. The start ants are Combat Ants.

**The breach** (what a bot alone on the map does, `GateGoalTask` with the shore breach of `plan_gate_goal`): 1. a home Bomber power-up makes a Bomber; 2. it plants a bomb and hops over the fire wall onto the field's Fire power-up (a chain of 2 bombs: it lands as planned 64 percent of the time, every bomb being a dud 1 time in 5); 3. the Fire Ant bounces home over map bombs (`GateInfo::best_map_bomb_bounce`, 2 more bombs); 4. it puts out the fire cell at the shore end of the walk to the lake; 5. it takes a home Bomber power-up again (leaving the Fire one) and defuses the bombs of that walk, one after the other; 6. a Swimmer (a field power-up, reached by another hop) swims to the pile, and carries the food home along the cleared walk, 50 points a trip. Alone on the map a Hard bot gets through in about a third of the seat-matches (the table above), and then banks most of the 2,500 points.

**Why it fails in a match** (one against one and four bots: 0 points per seat, also with the rule above):

1. **The start ants are dealt across both halves.** Teams 0 and 1 have one start ant in their own half and two in the other, teams 2 and 3 two at home and one away. Only an ant of the own half can do the breach (the home power-ups, the hop tiles and the corridor are there), and in a match every half holds Combat Ants of two to four teams, which fight at once (the home ants attack the strangers first). The one home ant of team 0 or 1 dies or is held by the fight before it has its Bomber; the gate goal waits for an ant that never comes (`Fetch`) and gives the attempt up after 90 seconds, three times, then the bot plays as if there were no gates. A team that still has ants in the other half gets no free egg at home either.
2. **Duds strand ants.** A chain that stops at a dud leaves the ant on a bomb-free pocket of the field. A stranded Bomber could defuse its way out (defusing costs no hit points) but the attempt is given up as stranded; a stranded Fire Ant (2 hit points after 4 bombs) has no way at all. Alone on the map this is the most common failure.
3. **A hop onto a power-up prefers to land on its tile**, but a flight takes nothing: the ant must step off and back on, which the lake's edge does not allow (water and bombs all round), so the attempt ends as stranded.
4. **Time goes to the wrong things** once the Fire Ant is home: the wall keeper puts out the whole fire wall row (about 17 cells, 1,500 ticks), and the trips for power-ups switch the one home ant between a Bomber and a Fire power-up for a while (the own side's power-ups are wanted, and a typed ant that takes another power-up leaves its old one on the tile).

**Tried and left out** (Hard, against the bot without it):

- **The next goal at once** (an explicit claim of the gate goal on its ant: when a goal is done, plan the next one in the same look and take its ant before the other tasks look, instead of 20 ticks later). Alone on POPcOrN it lifted a Hard bot from 415 to 464 points, but with the rule above it added nothing (606 against 619), and it cost the vault raids of the HELLRAISER family in matches: duels 43.6 +- 9.2 percent of the share on HELLRAISER and 26.0 +- 16.7 on HELLRAISER III (32 seeds x 2 seatings), and 30.3 +- 24.7 on HELLRAISER with four seats. A likely cause, not yet checked: the next raid takes the Bomber at once with the hit points for the way in but not for the way back.
- **No job for a target that no walk reaches** (the counters, the mines, the wall keeper; the old work-in-progress `reach`): it ended the refused orders but cost Ghetto 19 points of share in duels (31.0 +- 14.9 percent, 8 seeds x 2 seatings), ANTS_PARTY and FKD Deathmatch about 5 each, and was no better on POPcOrN than the rule above.
- **A stranded Bomber that defuses its way to the target**: a first version changed nothing (the cases it was meant for ended before it, in a walk home to heal that no walk could make); not finished.
- **Holding the walls and the own side's power-up trips back for the whole breach** instead of the first 2,400 ticks: one seat-match of 32 more (noise).

**Next** (in this order): keep the home ant of a breach out of fights it does not have to take while the gate goal waits for its ant (and let the ants of the other half fight or harass there); land next to a Swimmer power-up rather than on it; let a stranded Bomber dig toward the goal; leave the map's own fire walls to the gate goal, as its bombs; then a claim of the gate goal on its ant that checks the hit points for the whole raid. Measure with `bot_arena --map Community-Maps/POPcOrN.lvl` one against one and with four bots, as well as alone (`--seat N=standard:hard` for one seat), and on the HELLRAISER family.
