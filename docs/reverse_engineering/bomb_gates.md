# Bomb throw, knockback, fire walls, defuse and power-up facts

What the simulation does, pinned by `tests/test_sim/test_bomb_facts.cpp` (suite 2.9.5) as probes: no rule, no bot and no hash was changed. A planner (a bot that bombs itself over a fire wall) can rely on every line below; the tests fail when one of them stops being true. Directions are 0 N, 1 NE, 2 E, 3 SE, 4 S, 5 SW, 6 W, 7 NW. The code is in `combat_system.cpp` (`landing_ok`, `knock_dir`, `pick_landing`, `bomb_trigger`, `bomb_victim`), `movement_system.cpp` (the walk step, `can_enter`, `step_cost`, `approach_tile`) and `ability_system.cpp`.

## The bomb throw

| Fact | Value |
|---|---|
| Who sets a bomb off | An ant whose walk ends on the bomb tile, or whose flight lands on it (any type, any team, any owner). The path planner routes around bombs of the ant's own team and of its ally; enemy and neutral (map file, owner 255) bombs are walked into. An order to the bomb tile itself is allowed for an own bomb (an ant can bomb itself). |
| Direction | The OPPOSITE of the ant's facing at that moment (`facing + 4`); the facing is the direction of its last step, so an ant that walks north onto a bomb flies south. A hit-knocked ant keeps the facing toward its attacker, so it flies on in the knock direction. |
| Range | Exactly 4 tiles in a straight line (8 directions: 4 columns and 4 rows for a diagonal). |
| Blocked landing | Only the landing tile is tested, never the tiles between: a rock, the map edge, a hill (its 4x4) or the hill's queue tiles refuse it (the special tiles of a team that has dropped out are ordinary ground). Then the directions d+1, d+2, ... d+7 are tried in turn (clockwise). Water, an ant, a bomb or a fire wall on the landing tile do not refuse it (a fire wall counts as free ground although it is solid). |
| No free tile at all | No flight: the ant burns on the spot like a dud. |
| Cost | 2 hit points (credited to the bomb owner's team); the bomb is removed (also when it is a dud). An ant at 2 hp dies; a 3 hp ant survives at 1 hp (and then goes home). |
| Dud | One draw of the match generator (`rand() % 100 < 20`): about 20 percent (201 of the seeds 1 .. 1000). A dud: no flight, the ant stays on the tile under the burn overlay, then stunned. The draw is deterministic given the seed and the draws before it (each `spawn_unit` draws once for the random facing). |
| Chain | A flight that lands on a bomb sets it off at once (each one draws for its own dud) and throws the ant 4 more tiles in the same direction (the flight leaves the facing as it was); 2 hp each. A dud or a landing without a bomb ends the chain; when a landing needed the rotation, the next hop goes in the rotated direction (the flight faces the way it actually went). |
| Landing | On water a non-swimmer drowns (a swimmer splashes, stunned). On a fire wall a non-fire ant loses 1 hp and is thrown 1 tile to a random free neighbour; a fire ant stays on the cell, stunned, unhurt. On a power-up the ant does not take it. |

## Hit knockback

A hit throws the victim 1 tile (4 tiles when the attacker is a Combat ant, counted from the victim's tile) in the direction from the attacker's tile through the victim's; 1 hp (Combat 2 hp). A refused landing tries the directions +1, -1, +2, -2 of that direction at the same range; with all five refused the victim does not fly (hit point lost, stunned in place). At a map edge the outside tiles are refused like rocks. A victim knocked onto a bomb sets it off and flies on 4 tiles in the knock direction.

## Fire walls

- Only a Fire ant enters a fire cell (a wall across the whole map stops every other ant, swimmers included: there is no path); a Fire ant walks through unhurt.
- Extinguish: only a Fire ant, by the special order on the wall. It works from a cardinal neighbour (an ant at a diagonal walks to one) and takes 26 ticks from the north side, 28 from the others (clip 1200 / 1300 ms, measured by polling in 50 ms ticks).
- A flight may land on a fire cell (see the throw table).

## Defuse

Only a Bomber does it (the special order on a bomb tile); a Worker, Thief or Combat ant given that order walks onto the bomb and sets it off, a Fire ant or Swimmer is refused. It works from a cardinal neighbour (a diagonal start walks to one), takes 24 ticks from the start of the clip to the bomb gone, the bomb is removed with no explosion and no damage, the ant stays where it worked; bombs of any owner can be defused.

## Power-ups

A power-up is taken only when a move ends on its tile. Walking over a map-file power-up (solid bit clear, as on POPcOrN) does not take it; a flight that lands on it does not take it; a move order to the ant's own tile afterwards does.

## Gate analysis (ants_ai)

`GateInfo` (`include/ants_ai/gate_info.hpp`, `src/ants_ai/gate_info.cpp`) turns the facts above into an analysis of a map for one team. It is built once from `MapInfo` and the grid, like `IslandInfo`, changes no behaviour (no task uses it yet) and reads the map at the start; every special tile of a hill counts as live. Tests: `tests/test_ai/test_ai_gates.cpp` (AI26): the throw rule against the engine tile by tile on POPcOrN, hand-made worlds, POPcOrN, Bombz Away, and every map of the library.

- **Safe components.** The walker components with fire walls AND bombs closed (a bomb tile is walkable for the path finder, but whoever steps on it is thrown). `home()` is the component the hill's ants leave into (-1 on Bombz Away, where a bomb lies on every tile in front of every ramp).
- **Per target** (every pile and power-up): `gate` is `None` (reached with both closed), `Fire` (only a Fire ant's walk through fire cells reaches it), `Bombs` (only a walk over defused bombs), `Either`, `Mixed` (a walk exists only through fire and bombs together) or `Sealed` (no walk). `fire_cells` and `defuse_bombs` are the fewest fire cells / bombs on such a walk (-1: none). `passes` says which means get past: `kPassFireAnt` (a Fire ant's walk), `kPassDefuse` (a Bomber defuses the bombs of the walk), `kPassHop` (a hop chain lands in reach).
- **Hops.** `hops()` are the Bomber hops from the home component that end on ground of another component: the bomb tile T is free bomb ground with a cardinal neighbour in the home component (where the Bomber works from), the ant steps onto T from a neighbour P of the home component in the facing P -> T, and `GateInfo::simulate` plays the rule: opposite the facing, 4 tiles, clockwise retry, a landing on a map bomb throws again in the facing the flight left, 2 hp and 20 percent dud per bomb (`fly_percent`, `hp`). A chain that ends in water, on a fire wall or stuck is not a way over and is not listed. The best hop of a target has the fewest bombs, then the best chance.
- **Numbers.** POPcOrN: 62 fire cells in two walls of 31, 249 bombs; per team 11 power-ups are free (the home half), 11 are `Mixed` (the field, behind fire and bombs), 10 are `Sealed`, and the pile (in the lake) is `Sealed`; 42 hops from each north hill, 44 and 45 from the south hills, 17 of the 32 power-ups are in reach of a hop. Bombz Away: 895 bombs, the pile is `Bombs` (a tunnel of defused bombs, `passes == kPassDefuse`) for every team.

The 2-hop chains from the north half that end ON a power-up (the tables below list every free tile: the test clears the start ants first, since a tile with an ant takes no bomb): T(3,7) f0, T(8,7) f7, T(9,7) f1, T(21,7) f5 / f7, T(22,7) f1, and T(27,7) and T(29,7) in the facings 0, 5, 6, 7.

## Gate goals (ants_ai, Hard, off)

`plan_gate_goal` (`include/ants_ai/gate_goal.hpp`) chooses what a bot whose food is walled off sets out to do, and `GateGoalTask` (`gate_task.hpp`) works it. Tests: `tests/test_ai/test_ai_gate_goal.cpp` (AI27). It plays only at Hard and only with `LevelPlan::gate_goals`, which is **off in every plan** (`--tune ggoal=1` switches it on for a tournament run; `gstep`, `gattempt`, `gretries` and `ghop` set its numbers). No other bot, task or rule changes while it is off.

- **The goal.** It exists only while no pile with units left is free of every gate (food the hill reaches is normal play). The cheapest of: **Extinguish** (a pile behind fire walls only: a Fire ant puts out the first fire cell of the walk with the fewest, `GateTarget::fire_cell`; cost 100 + 10 per cell), **Hop** onto a power-up of a wanted kind (Swimmer, Fire) that a hop chain of at most `gate_hop_bombs` (3) bombs reaches, preferring a chain that lands ON the tile (200 + 40 per bomb), **Hop** in reach of a pile (400 + 40 per bomb). A kind with no source (no own ant of it on the hill's side of the gates, a mound tile counting as the hill's side, and no power-up of it there) makes its goals impossible, so they are not planned: a typed ant leaves its old power-up behind when it takes a new one, and on the far side of a wall that one is no source.
- **The right ant first.** The task takes an own ant of the needed type (Fire for Extinguish, Bomber for Hop) from the lower ranks (its rank is above the fight's: a goal exists only while the bot banks nothing), on the hill's side of the walls and with the hit points for the chain (2 per bomb and one more). Without one it WANTS one (`Tactics::wants`, one more than the ants that cannot serve) and the power-up task fetches it. On POPcOrN the start ants of a team lie on both sides of the fire walls, and a Combat default ant that takes a Thief power-up in the opening is gone as an ant for a Bomber: a team with one ant on its side has none to spare (the later pull requests of the series deal with that).
- **The act.** Extinguish: a special order onto the fire cell (the ant walks to a cardinal neighbour and puts it out; done when the cell is no longer fire). Hop: the Bomber plants on the bomb tile T, steps to the tile P in front of it and is clicked onto the bomb (an own bomb is no obstacle for an order onto it); a bomb that was a dud (the ant burns on the spot) is planted again while the ant has the hit points; the ant that lands where the chain ends is done, and for a power-up it is clicked onto it (a flight takes nothing, a move that ends on the tile does).
- **The times** (the owner's numbers, ticks of 50 ms): a step with no progress for 1,800 ticks (90 s) gives up the attempt, an attempt not done after 4,800 (4 minutes) too; two retries follow, then the task is exhausted and the bot plays as if there were no gates (a new source of its ant, such as a Bomber hatched or a power-up on the hill's side, starts it afresh, at most twice; the bot's fallback to the plain economy gives the held ant back and drops the attempt without counting it). No attempt starts with less than 2,400 ticks left. The task's rank (6) is above the fights: a goal exists only while the bot banks nothing.
- **Later pull requests** of the series: the bomb as a weapon, and the arena harness that measures the gain.

## Bomb routing (ants_ai, Hard, off)

`RouteTask` (`include/ants_ai/route_task.hpp`, `src/ants_ai/route_task.cpp`; tests `tests/test_ai/test_ai_route.cpp`, AI28) is the Bombz Away answer: a map whose floor is one bomb field (every tile a neutral bomb, owner 255), where every ant of a team starts on a bomb. It plays at Hard with `LevelPlan::gate_goals` (off in every plan; `--tune ggoal=1`); below that flag `LevelPlan::gate_route` (on, `--tune groute=0` leaves the bombs to the goals' hop, as AI27 tests). Rank 5. It does nothing on a map with no bomb of another team.

- **The owner's rules.** A Bomber clears the bombs on its way from where it stands to the food and from the food to its own base, and no others: another team's bomb area is never touched, and a Bomber does not help another Bomber's area. It may have to defuse a way to the right side first. Once an ant is a Bomber it does not run into bombs.
- **The way.** The engine's own walk, predicted with `ants::sim::PathSearch` on `MapInfo::step_cost` (bombs carry no path cost, so defusing one does not change the walk): from each Bomber's tile to the click tile of the nearest pile, from the workers' tiles, and from the hill's start tiles to the pile and back. A walk ends ON its goal and the path does not list it, so the tiles where a walk ends count too: the pile's cells, its click tile and the eight tiles round them (the workers stand there when they harvest); the hill's start tiles are the other end of the walks. Bombz Away needs 13, 8, 12 and 9 bombs for the walk in (AI26.5); the whole job, base leg and the pile's edge included, is about 50 defuses (AI28.5).
- **The act.** Each Bomber (at most four) defuses by the real rule: a special order onto a bomb from a tile next to it (N, S, W or E; 24 ticks; an order from afar would have the engine walk the ant to such a tile over whatever lies between, bombs too, so the task never orders from afar). It steps only onto tiles without a bomb, one step per order; a bomb that only a diagonal step touches is reached through a cardinal neighbour of it. A bomb another Bomber is at work on is left to it (a special order onto a tile without a bomb would plant one).
- **The economy** keeps off the pile (`HarvestTask::set_closed_piles`) until the way has no bomb. When the way is clear (checked every 60 ticks) the Bombers are given back to the economy at once: a Bomber is the default ant of Bombz Away and often the only ant a team has, so holding it would starve the team. A way with bombs again (a bomb planted by someone) takes a Bomber again. A route that makes no progress for 1,800 ticks (90 s) is given up for good and the pile opens (the Bombers are released on every exit, also in the bot's fallback).
- **One task of the standard bot yields to it** under the same flags: the bomb counter (`BombTask`, rank 4) defuses an enemy bomb near the hill or the pile only from a tile next to it (its order from afar had the engine walk the Bomber across the bomb field). Gate goals keep their own Bomber (rank 6) and a pile that the goal planner calls a gate by bombs stays theirs when the way is a hop.
- **The opening.** A Bomber that stands on a bomb sets it off when it works without walking (the defuse from its own tile ends a walk of no steps on the bomb, engine `walk_step`), and the blast also hits the ants on the tiles next to it. A Bomber first walks off onto a tile without a bomb when it has one; on Bombz Away every start ant is boxed in, so the healthiest takes the one hit (it is thrown 4 tiles and on through the bombs it lands on, 2 hp each), the others wait, and the thrown Bomber lands on ground without a bomb and clears the way. After that no Bomber steps on a bomb (AI28.2).
- **Numbers** (`bot_arena`, Bombz Away, four Hard seats, whole matches, seeds 1 to 16 = 64 seats, banked score): flag off 9,950 in all (mean 155, median 150, 49 seats above zero, best 450); flag on 30,950 (mean 484, median 550, 61 seats above zero, best 1,350). Seed 1: off 400/0/100/450, on 650/50/750/550. The flag off plays tick for tick as before (the six shipped maps, POPcOrN and Bombz Away seeds 1 to 3 have the same hashes as main).

## HELLRAISER and its family: what GateInfo reads (no behaviour yet)

On these maps the start ants do not stand on the side of their own hill: the hill (and so `GateInfo::home()`) is in one quarter of the map, the three start ants of the team in another, and the food in the middle is in a third component; fire walls and bombs are not the gate, rock walls are (`fire_cell_count` and `bomb_cell_count` are 0 on HELLRAISER, II, III and ShOrT hElLrAiSeR). `GateInfo::hops()` lists hops from the HOME component only, so for these maps it finds the hop outward from the hill and never the ones the ants need. Hop graph between the walker components (one bomb is one hop: planted on free ground, the ant steps onto it from a neighbour; `GateInfo::simulate`), per seat:

| Map | start component to food | food to own hill | start to hill |
|---|---|---|---|
| HELLRAISER, HELLRAISER III, ShOrT hElLrAiSeR | 1 bomb | 1 bomb | 2 bombs (4 hp) |
| HELLRAISER II | no component touches the food | - | 2 bombs |
| HeLLRaiSeR V3.1 | one component, bombs 24, the 4 piles touch no walker component (water or rock, not a hop) | - | - |
| HeLLRaiSeR v3.0 | fire 8, bombs 10, piles Mixed or Sealed | - | 2 bombs |
| POPcOrN | 2 bombs | none back | see above |

The four seats of one map are the same by symmetry. The human replay of the owner on HELLRAISER (seat 0, Green; hill top left at about (5,5), start ants at (21,21), food at (15,15)): a start ant took the Bomber power-up at (30,30); the Bomber planted at (17,20) (special order, 20.7 s), stepped onto it from the north (a dud: 2 hp, no flight), planted at (16,20) (41 s), stepped onto it from (16,19) facing south and flew north to (16,16) (53 s, 6 hp left), walked to the food (15,15), crossed to (13,14), planted at (14,14) (61 s) and stepped east onto it: flew west to (10,14) (70 s, 4 hp left), then walked (11,10), (10,4), (5,6) to the hill and banked 50 at 81 s. A Red thief stole it at once. So the human used exactly the two hops the table gives (food leg and base leg), three bombs with one dud, 6 of 10 hit points. The Hard bot of the match sent its Combat ants to fight and one Thief to steal; it never planned a hop.

What a next pull request needs: (1) `GateInfo` hops from every component, not only the home one, and a hop graph over components (a shortest path in bombs and hit points: 2 hp per bomb, 20 percent dud, the ant needs hp for them and one more); (2) `plan_gate_goal` goals from where the ants are: a food leg (start component to food) and a base leg (food to hill), with the Bomber source on the start side (a start ant that turns into a Bomber through the power-up at the edge of its component, as the human did); (3) the carry: the Bomber (or any ant) that crosses with food has to be the one that banks, so the goal must keep food and Bomber together, or hop every worker; (4) the opponent's thief at the hill (the human lost its 50 at once). Nothing of this is in this pull request.

## POPcOrN (Community-Maps/POPcOrN.lvl)

Rows 9 and 21 are fire walls across the whole map; rows 10 .. 20 are a field of map bombs (owner 255) with a lake in the middle (row 14 is mostly water: a landing there drowns the ant); the home halves are rows 0 .. 8 (north: teams 0 and 2) and 22 .. 30 (south). A bomb tile in row 5 or lower throws a north-facing ant onto the wall (row 9, 4 rows away), rows 6 .. 8 over it. The real play pinned by the test: the Bomber plants at (3,7) from the lane (3,8), steps north onto it and flies (3,7) to (3,11) (map bomb) to (3,15) (power-up), 4 hp lost; 28 of 40 seeds fly both hops, the rest stop at the first dud.

Reading the tables: `T(x,y)` is the bomb tile, `fN` the facing of the ant on it (`*`: the ant can walk onto T in that facing, the tile behind it is open ground), then the landing of each hop (`.` plain, `B` map bomb, which sets off again, `P` power-up, `~` water, `F` fire wall). Only facings whose first landing reaches the wall (row 9 or beyond; south half: row 21 or beyond) are listed. Each bomb costs 2 hp: a chain of n hops costs 2n (a worker has 10).

The 2-hop chains that end on a power-up: see the end of the gate analysis above (T(27,7) and T(29,7) in the facings 5, 6, 7 get there by the rotation, since the straight landing east or west is outside the map).

### North half

north totals: 222 hops over the wall, 56 onto the wall, 196 over the wall with a chain of 2 or more hops, 26 chains end on a power-up (16 of them after exactly 2 hops)

```text
T(0,5): f0* (0,9)F | f6* (4,9)F | f7* (4,9)F
T(1,5): f0* (1,9)F | f6* (5,9)F | f7* (5,9)F
T(2,5): f0* (2,9)F | f6* (6,9)F | f7* (6,9)F
T(3,5): f0* (3,9)F | f6 (7,9)F | f7 (7,9)F
T(8,5): f0* (8,9)F | f1 (4,9)F | f6* (12,9)F | f7* (12,9)F
T(9,5): f0* (9,9)F | f1* (5,9)F | f6 (13,9)F | f7 (13,9)F
T(11,5): f0* (11,9)F | f1 (7,9)F | f6 (15,9)F | f7* (15,9)F
T(19,5): f0* (19,9)F | f1* (15,9)F | f6 (23,9)F | f7 (23,9)F
T(21,5): f0* (21,9)F | f1 (17,9)F | f6* (25,9)F | f7* (25,9)F
T(22,5): f0* (22,9)F | f1* (18,9)F | f6 (26,9)F | f7 (26,9)F
T(27,5): f0* (27,9)F | f1 (23,9)F | f5* (27,9)F | f6* (27,9)F | f7* (27,9)F
T(28,5): f0* (28,9)F | f1* (24,9)F | f5* (28,9)F | f6* (28,9)F | f7* (28,9)F
T(29,5): f0* (29,9)F | f1* (25,9)F | f5* (29,9)F | f6* (29,9)F | f7* (29,9)F
T(30,5): f0* (30,9)F | f1* (26,9)F | f5 (30,9)F | f6 (30,9)F | f7 (30,9)F
T(0,6): f0* (0,10)B (0,14)~ | f6* (4,10)B (8,14)~ | f7* (4,10)B (8,14)~
T(1,6): f0* (1,10)B (1,14)~ | f6* (5,10)B (9,14)~ | f7* (5,10)B (9,14)~
T(3,6): f0* (3,10)B (3,14)~ | f6 (7,10)B (11,14)~ | f7 (7,10)B (11,14)~
T(8,6): f0* (8,10)B (8,14)~ | f1 (4,10)B (0,14)~ | f7* (12,10)B (16,14)B (20,18)B (20,22).
T(9,6): f0* (9,10)B (9,14)~ | f1* (5,10)B (1,14)~ | f7 (13,10)B (17,14)~
T(11,6): f0 (11,10)B (11,14)~ | f1 (7,10)B (3,14)~ | f7 (15,10)B (19,14)~
T(12,6): f0 (12,10)B (12,14)B (12,18)B (12,22). | f1 (8,10)B (4,14)~ | f7 (16,10)B (20,14)~
T(13,6): f0 (13,10)B (13,14)~ | f1 (9,10)B (5,14)~ | f7* (17,10)B (21,14)~
T(14,6): f0* (14,10)B (14,14)B (14,18)~ | f1 (10,10)B (6,14)~ | f7* (18,10)B (22,14)~
T(15,6): f0* (15,10)B (15,14)B (15,18)~ | f1* (11,10)B (7,14)~ | f7* (19,10)B (23,14)~
T(16,6): f0* (16,10)B (16,14)B (16,18)~ | f1* (12,10)B (8,14)~ | f6* (20,10)B (24,14)~ | f7 (20,10)B (24,14)~
T(17,6): f0 (17,10)B (17,14)~ | f1* (13,10)B (9,14)~ | f7 (21,10)B (25,14)~
T(18,6): f0 (18,10)B (18,14)B (18,18)B (18,22). | f1 (14,10)B (10,14)~ | f7 (22,10)B (26,14)~
T(19,6): f0 (19,10)B (19,14)~ | f1 (15,10)B (11,14)~ | f6 (23,10)B (27,14)~ | f7 (23,10)B (27,14)~
T(21,6): f0* (21,10)B (21,14)~ | f1 (17,10)B (13,14)~ | f6* (25,10)B (29,14)~ | f7* (25,10)B (29,14)~
T(22,6): f0* (22,10)B (22,14)~ | f1* (18,10)B (14,14)B (10,18)B (6,18). | f6 (26,10)B (30,14)~ | f7 (26,10)B (30,14)~
T(27,6): f0* (27,10)B (27,14)~ | f1 (23,10)B (19,14)~ | f5* (27,10)B (27,14)~ | f6* (27,10)B (27,14)~ | f7* (27,10)B (27,14)~
T(28,6): f0* (28,10)B (28,14)~ | f1* (24,10)B (20,14)~ | f5* (28,10)B (28,14)~ | f6* (28,10)B (28,14)~ | f7* (28,10)B (28,14)~
T(29,6): f0* (29,10)B (29,14)~ | f1* (25,10)B (21,14)~ | f5* (29,10)B (29,14)~ | f6* (29,10)B (29,14)~ | f7* (29,10)B (29,14)~
T(30,6): f0* (30,10). | f1* (26,10)B (22,14)~ | f5 (30,10). | f6 (30,10). | f7 (30,10).
T(0,7): f0* (0,11)B (0,15)~ | f5* (4,11)B (8,15)B (12,19)B (16,23)P | f6* (4,11)B (8,15)B (12,19)B (16,23)P | f7* (4,11)B (8,15)B (12,19)B (16,23)P
T(1,7): f0* (1,11). | f5* (5,11)B (9,15)B (13,19)B (9,23). | f6* (5,11)B (9,15)B (13,19)B (9,23). | f7* (5,11)B (9,15)B (13,19)B (9,23).
T(2,7): f0* (2,11)B (2,15)B (2,19)B (2,23). | f5* (6,11)B (10,15)B (14,19)B (14,23)P | f6* (6,11)B (10,15)B (14,19)B (14,23)P | f7* (6,11)B (10,15)B (14,19)B (14,23)P
T(3,7): f0* (3,11)B (3,15)P | f6 (7,11)B (11,15)B (15,19)B (15,23)P | f7* (7,11)B (11,15)B (15,19)B (15,23)P
T(8,7): f0* (8,11)B (8,15)B (8,19)B (8,23). | f1* (4,11)B (0,15)~ | f6* (12,11)B (16,15)P | f7* (12,11)B (16,15)P
T(9,7): f0* (9,11)B (9,15)B (9,19)B (9,23). | f1* (5,11)B (1,15)P | f5 (13,11)B (17,15)~ | f6 (13,11)B (17,15)~ | f7* (13,11)B (17,15)~
T(21,7): f0* (21,11)B (21,15)B (21,19)B (21,23). | f1* (17,11)B (13,15)~ | f5* (25,11)B (29,15)P | f6* (25,11)B (29,15)P | f7* (25,11)B (29,15)P
T(22,7): f0* (22,11)B (22,15)B (22,19)B (22,23). | f1* (18,11)B (14,15)P | f6 (26,11)B (30,15)~ | f7* (26,11)B (30,15)~
T(27,7): f0* (27,11)B (27,15)P | f1* (23,11)B (19,15)B (15,19)B (11,19)B (7,19)B (3,19)B (3,15)P | f5* (27,11)B (27,15)P | f6* (27,11)B (27,15)P | f7* (27,11)B (27,15)P
T(28,7): f0* (28,11)B (28,15)B (28,19)B (28,23). | f1* (24,11)B (20,15)B (16,19)B (12,19)B (8,19)B (4,19)B (0,19)B (0,15)~ | f5* (28,11)B (28,15)B (28,19)B (28,23). | f6* (28,11)B (28,15)B (28,19)B (28,23). | f7* (28,11)B (28,15)B (28,19)B (28,23).
T(29,7): f0* (29,11)B (29,15)P | f1* (25,11)B (21,15)B (17,19)B (13,19)B (9,19)B (5,19)B (1,19). | f5* (29,11)B (29,15)P | f6* (29,11)B (29,15)P | f7* (29,11)B (29,15)P
T(30,7): f0* (30,11)B (30,15)~ | f1* (26,11)B (22,15)B (18,19)B (14,23)P | f5 (30,11)B (30,15)~ | f6 (30,11)B (30,15)~ | f7 (30,11)B (30,15)~
T(0,8): f0 (0,12)B (0,16)~ | f7 (4,12)B (8,16)~
T(1,8): f0 (1,12)B (1,16)~ | f7 (5,12)B (9,16)~
T(2,8): f0 (2,12)B (2,16)~ | f7 (6,12)B (10,16)~
T(3,8): f0 (3,12)B (3,16)~ | f7 (7,12)B (11,16)~
T(4,8): f0 (4,12)B (4,16)~ | f1 (0,12)B (0,8). | f7 (8,12).
T(5,8): f0 (5,12)B (5,16)~ | f1 (1,12)B (1,8). | f7 (9,12)B (13,16)~
T(6,8): f0 (6,12)B (6,16)~ | f1 (2,12)B (2,8). | f7 (10,12)B (14,16)B (18,20)B (22,24).
T(7,8): f0 (7,12)B (7,16)B (7,20)B (3,24). | f1 (3,12)B (3,8). | f7 (11,12)B (15,16)B (19,20)B (19,24).
T(8,8): f0 (8,12). | f1 (4,12)B (0,16)~ | f7 (12,12)B (16,16)B (20,20)B (16,24).
T(9,8): f0 (9,12)B (9,16)~ | f1 (5,12)B (1,16)~ | f7 (13,12)~
T(10,8): f0 (10,12)B (10,16)~ | f1 (6,12)B (2,16)~ | f7 (14,12)~
T(11,8): f0 (11,12)B (11,16)~ | f1 (7,12)B (3,16)~ | f7 (15,12)~
T(12,8): f0 (12,12)B (12,16)~ | f1 (8,12). | f7 (16,12)~
T(13,8): f0 (13,12)~ | f1 (9,12)B (5,16)~ | f7 (17,12)~
T(14,8): f0 (14,12)~ | f1 (10,12)B (6,16)~ | f7 (18,12)B (22,16)~
T(15,8): f0 (15,12)~ | f1 (11,12)B (7,16)B (3,20)B (3,16)~ | f7 (19,12)B (23,16)B (27,20)B (27,24).
T(16,8): f0 (16,12)~ | f1 (12,12)B (8,16)~ | f7 (20,12)B (24,16)~
T(17,8): f0 (17,12)~ | f1 (13,12)~ | f7 (21,12)B (25,16)~
T(18,8): f0 (18,12)B (18,16)~ | f1 (14,12)~ | f7 (22,12)B (26,16)~
T(19,8): f0 (19,12)B (19,16)~ | f1 (15,12)~ | f7 (23,12)B (27,16)~
T(20,8): f0 (20,12)B (20,16)~ | f1 (16,12)~ | f7 (24,12).
T(21,8): f0 (21,12)B (21,16)~ | f1 (17,12)~ | f7 (25,12)B (29,16)~
T(22,8): f0 (22,12)B (22,16)~ | f1 (18,12)B (14,16)B (10,20)B (6,20)B (2,20)B (2,16)~ | f7 (26,12)B (30,16)~
T(23,8): f0 (23,12)B (23,16)B (23,20)B (19,24). | f1 (19,12)B (15,16)B (11,20)B (7,20)B (3,20)B (3,16)~ | f7 (27,12)B (27,16)~
T(24,8): f0 (24,12). | f1 (20,12)B (16,16)B (12,20)B (8,24). | f7 (28,12)B (28,16)~
T(25,8): f0 (25,12)B (25,16)~ | f1 (21,12)B (17,16)~ | f7 (29,12)B (29,16)~
T(26,8): f0 (26,12)B (26,16)~ | f1 (22,12)B (18,16)~ | f7 (30,12)B (30,16)~
T(27,8): f0 (27,12)B (27,16)~ | f1 (23,12)B (19,16)~ | f5* (27,12)B (27,16)~ | f6* (27,12)B (27,16)~ | f7 (27,12)B (27,16)~
T(28,8): f0 (28,12)B (28,16)~ | f1 (24,12). | f5* (28,12)B (28,16)~ | f6* (28,12)B (28,16)~ | f7 (28,12)B (28,16)~
T(29,8): f0 (29,12)B (29,16)~ | f1 (25,12)B (21,16)~ | f5* (29,12)B (29,16)~ | f6* (29,12)B (29,16)~ | f7 (29,12)B (29,16)~
T(30,8): f0 (30,12)B (30,16)~ | f1 (26,12)B (22,16)~ | f5 (30,12)B (30,16)~ | f6 (30,12)B (30,16)~ | f7 (30,12)B (30,16)~
```

### South half

south totals: 216 hops over the wall, 56 onto the wall, 183 over the wall with a chain of 2 or more hops, 21 chains end on a power-up (12 of them after exactly 2 hops)

```text
T(0,22): f1 (0,18)B (0,14)~ | f2 (0,18)B (0,14)~ | f3 (0,18)B (0,14)~ | f4 (0,18)B (0,14)~ | f5 (4,18)B (8,14)~
T(1,22): f1* (1,18)B (1,14)~ | f2* (1,18)B (1,14)~ | f3 (1,18)B (1,14)~ | f4 (1,18)B (1,14)~ | f5 (5,18)B (9,14)~
T(2,22): f1* (2,18)B (2,14)~ | f2* (2,18)B (2,14)~ | f3 (2,18)B (2,14)~ | f4 (2,18)B (2,14)~ | f5 (6,18).
T(3,22): f1* (3,18)B (3,14)~ | f2* (3,18)B (3,14)~ | f3 (3,18)B (3,14)~ | f4 (3,18)B (3,14)~ | f5 (7,18)B (11,14)~
T(7,22): f3 (3,18)B (3,14)~ | f4 (7,18)B (7,14)~ | f5 (11,18)B (15,14)B (19,10)B (23,10)B (27,10)B (27,14)~
T(8,22): f1 (4,18)B (0,14)~ | f2* (4,18)B (0,14)~ | f3 (4,18)B (0,14)~ | f4 (8,18). | f5 (12,18)B (16,14)B (20,10)B (24,10)B (28,10)B (28,14)~
T(9,22): f1* (5,18)B (1,14)~ | f2* (5,18)B (1,14)~ | f3 (5,18)B (1,14)~ | f4 (9,18)B (9,14)~ | f5 (13,18)~
T(10,22): f0 (6,18). | f1* (6,18). | f2* (6,18). | f3 (6,18). | f4 (10,18)B (10,14)~ | f5 (14,18)~
T(11,22): f3 (7,18)B (3,14)~ | f4 (11,18)B (11,14)~ | f5 (15,18)~
T(12,22): f3 (8,18). | f4 (12,18)B (12,14)B (12,10)B (12,6). | f5 (16,18)~
T(13,22): f3 (9,18)B (5,14)~ | f4 (13,18)~ | f5 (17,18)~
T(14,22): f3 (10,18)B (6,14)~ | f4 (14,18)~ | f5 (18,18)B (22,14)~
T(15,22): f3 (11,18)B (7,14)~ | f4 (15,18)~ | f5 (19,18)B (23,14)~
T(16,22): f3 (12,18)B (8,14)~ | f4 (16,18)~ | f5 (20,18)B (24,14)~
T(17,22): f3 (13,18)~ | f4 (17,18)~ | f5 (21,18)B (25,14)~
T(18,22): f3 (14,18)~ | f4 (18,18)B (18,14)B (18,10)B (18,6). | f5 (22,18)B (26,14)~
T(19,22): f3 (15,18)~ | f4 (19,18)B (19,14)~ | f5 (23,18)B (27,14)~
T(20,22): f3 (16,18)~ | f4 (20,18)B (20,14)~ | f5 (24,18).
T(21,22): f3 (17,18)~ | f4 (21,18)B (21,14)~ | f5 (25,18)B (29,14)~
T(22,22): f3 (18,18)B (14,14)B (10,10)B (14,6). | f4 (22,18)B (22,14)~ | f5 (26,18)B (30,14)~
T(26,22): f3 (22,18)B (18,14)B (14,10)B (14,6). | f4 (26,18)B (26,14)~ | f5 (30,18)B (30,22).
T(27,22): f1 (23,18)B (19,14)~ | f2* (23,18)B (19,14)~ | f3 (23,18)B (19,14)~ | f4 (27,18)B (27,14)~
T(28,22): f1* (24,18). | f2* (24,18). | f3 (24,18). | f4 (28,18)B (28,14)~
T(29,22): f1* (25,18)B (21,14)~ | f2* (25,18)B (21,14)~ | f3 (25,18)B (21,14)~ | f4 (29,18)B (29,14)~
T(30,22): f3 (26,18)B (22,14)~ | f4 (30,18)B (30,14)~
T(0,23): f1 (0,19)B (0,15)~ | f2 (0,19)B (0,15)~ | f3 (0,19)B (0,15)~ | f4* (0,19)B (0,15)~ | f5* (4,19)B (8,15)B (12,11)B (16,7)P
T(1,23): f1* (1,19). | f2* (1,19). | f3* (1,19). | f4* (1,19). | f5* (5,19)B (9,15)B (13,11)B (17,11)B (21,11)B (25,11)B (29,11)B (29,15)P
T(2,23): f1* (2,19)B (2,15)B (2,11)B (2,7). | f2* (2,19)B (2,15)B (2,11)B (2,7). | f3* (2,19)B (2,15)B (2,11)B (2,7). | f4* (2,19)B (2,15)B (2,11)B (2,7). | f5* (6,19)B (10,15)B (14,11)B (18,11)B (22,11)B (26,11)B (30,11)B (30,15)~
T(3,23): f1* (3,19)B (3,15)P | f2* (3,19)B (3,15)P | f3* (3,19)B (3,15)P | f4* (3,19)B (3,15)P | f5* (7,19)B (11,15)B (15,11)B (19,11)B (23,11)B (27,11)B (27,15)P
T(8,23): f2 (4,19)B (0,15)~ | f3* (4,19)B (0,15)~ | f4* (8,19)B (8,15)B (8,11)B (8,7). | f5* (12,19)B (16,15)P
T(9,23): f2* (5,19)B (1,15)P | f3* (5,19)B (1,15)P | f4* (9,19)B (9,15)B (9,11)B (9,7). | f5* (13,19)B (17,15)~
T(21,23): f1 (17,19)B (13,15)~ | f2 (17,19)B (13,15)~ | f3* (17,19)B (13,15)~ | f4* (21,19)B (21,15)B (21,11)B (21,7). | f5* (25,19)B (29,15)P
T(22,23): f2* (18,19)B (14,15)P | f3* (18,19)B (14,15)P | f4* (22,19)B (22,15)B (22,11)B (22,7). | f5* (26,19)B (30,15)~
T(27,23): f2 (23,19)B (19,15)B (15,11)B (15,7)P | f3* (23,19)B (19,15)B (15,11)B (15,7)P | f4* (27,19)B (27,15)P
T(28,23): f2* (24,19)B (20,15)B (16,11)B (16,7)P | f3* (24,19)B (20,15)B (16,11)B (16,7)P | f4* (28,19)B (28,15)B (28,11)B (28,7).
T(29,23): f2* (25,19)B (21,15)B (17,11)B (21,7). | f3* (25,19)B (21,15)B (17,11)B (21,7). | f4* (29,19)B (29,15)P
T(30,23): f2* (26,19)B (22,15)B (18,11)B (14,7)P | f3* (26,19)B (22,15)B (18,11)B (14,7)P | f4* (30,19)B (30,15)~
T(0,24): f1 (0,20)B (0,16)~ | f2 (0,20)B (0,16)~ | f3 (0,20)B (0,16)~ | f4* (0,20)B (0,16)~ | f5* (4,20)B (8,16)~
T(1,24): f1* (1,20)B (1,16)~ | f2* (1,20)B (1,16)~ | f3* (1,20)B (1,16)~ | f4* (1,20)B (1,16)~ | f5* (5,20)B (9,16)~
T(2,24): f1* (2,20)B (2,16)~ | f2* (2,20)B (2,16)~ | f3* (2,20)B (2,16)~ | f4* (2,20)B (2,16)~ | f5* (6,20)B (10,16)~
T(3,24): f1* (3,20)B (3,16)~ | f2* (3,20)B (3,16)~ | f3* (3,20)B (3,16)~ | f4* (3,20)B (3,16)~ | f5 (7,20)B (11,16)~
T(8,24): f2 (4,20)B (0,16)~ | f3 (4,20)B (0,16)~ | f4* (8,20)B (8,16)~ | f5* (12,20)B (16,16)B (20,12)B (24,8).
T(9,24): f2* (5,20)B (1,16)~ | f3* (5,20)B (1,16)~ | f4* (9,20)B (9,16)~ | f5 (13,20)B (17,16)~
T(11,24): f2 (7,20)B (3,16)~ | f3 (7,20)B (3,16)~ | f4 (11,20)B (11,16)~ | f5 (15,20)B (19,16)~
T(12,24): f3 (8,20)B (4,16)~ | f4 (12,20)B (12,16)~ | f5 (16,20)B (20,16)~
T(13,24): f3 (9,20)B (5,16)~ | f4 (13,20)B (13,16)~ | f5* (17,20)B (21,16)~
T(14,24): f2* (10,20)B (6,16)~ | f3 (10,20)B (6,16)~ | f4* (14,20)B (14,16)B (14,12)~ | f5* (18,20)B (22,16)~
T(15,24): f3* (11,20)B (7,16)B (3,12)B (3,8). | f4* (15,20)B (15,16)B (15,12)~ | f5* (19,20)B (23,16)B (27,12)B (27,16)~
T(16,24): f3* (12,20)B (8,16)~ | f4* (16,20)B (16,16)B (16,12)~ | f5 (20,20)B (24,16)~
T(17,24): f3* (13,20)B (9,16)~ | f4 (17,20)B (17,16)~ | f5 (21,20)B (25,16)~
T(18,24): f3 (14,20)B (10,16)~ | f4 (18,20)B (18,16)~ | f5 (22,20)B (26,16)~
T(19,24): f3 (15,20)B (11,16)~ | f4 (19,20)B (19,16)~ | f5 (23,20)B (27,16)~
T(21,24): f3 (17,20)B (13,16)~ | f4* (21,20)B (21,16)~ | f5* (25,20).
T(22,24): f3* (18,20)B (14,16)B (10,12)B (6,8). | f4* (22,20)B (22,16)~ | f5 (26,20)B (30,16)~
T(27,24): f2 (23,20)B (19,16)~ | f3 (23,20)B (19,16)~ | f4* (27,20)B (27,16)~
T(28,24): f2* (24,20)B (20,16)~ | f3* (24,20)B (20,16)~ | f4* (28,20)B (28,16)~
T(29,24): f2* (25,20). | f3* (25,20). | f4* (29,20)B (29,16)~
T(30,24): f2* (26,20)B (22,16)~ | f3* (26,20)B (22,16)~ | f4* (30,20)B (30,16)~
T(0,25): f1 (0,21)F | f2 (0,21)F | f3 (0,21)F | f4* (0,21)F | f5* (4,21)F
T(1,25): f1* (1,21)F | f2* (1,21)F | f3* (1,21)F | f4* (1,21)F | f5* (5,21)F
T(2,25): f1* (2,21)F | f2* (2,21)F | f3* (2,21)F | f4* (2,21)F | f5* (6,21)F
T(3,25): f1* (3,21)F | f2* (3,21)F | f3* (3,21)F | f4* (3,21)F | f5 (7,21)F
T(8,25): f2 (4,21)F | f3 (4,21)F | f4* (8,21)F | f5* (12,21)F
T(9,25): f2* (5,21)F | f3* (5,21)F | f4* (9,21)F | f5 (13,21)F
T(11,25): f2 (7,21)F | f3 (7,21)F | f4* (11,21)F | f5* (15,21)F
T(19,25): f2 (15,21)F | f3* (15,21)F | f4* (19,21)F | f5 (23,21)F
T(21,25): f2 (17,21)F | f3 (17,21)F | f4* (21,21)F | f5* (25,21)F
T(22,25): f2* (18,21)F | f3* (18,21)F | f4* (22,21)F | f5 (26,21)F
T(27,25): f2 (23,21)F | f3 (23,21)F | f4* (27,21)F
T(28,25): f2* (24,21)F | f3* (24,21)F | f4* (28,21)F
T(29,25): f2* (25,21)F | f3* (25,21)F | f4* (29,21)F
T(30,25): f2* (26,21)F | f3* (26,21)F | f4* (30,21)F
```
