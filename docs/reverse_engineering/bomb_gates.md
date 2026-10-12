# Bomb throw, knockback, fire walls, defuse and power-up facts

What the simulation does, pinned by `tests/test_sim/test_bomb_facts.cpp` (suite 2.9.5) as probes: no rule, no bot and no hash was changed. A planner (a bot that bombs itself over a fire wall) can rely on every line below; the tests fail when one of them stops being true. Directions are 0 N, 1 NE, 2 E, 3 SE, 4 S, 5 SW, 6 W, 7 NW. The code is in `combat_system.cpp` (`landing_ok`, `knock_dir`, `pick_landing`, `bomb_trigger`, `bomb_victim`), `movement_system.cpp` (the walk step, `can_enter`, `step_cost`, `approach_tile`) and `ability_system.cpp`.

## The bomb throw

| Fact | Value |
|---|---|
| Who sets a bomb off | An ant whose walk ends on the bomb tile, or whose flight lands on it (any type, any team, any owner). The path planner routes around bombs of the ant's own team and of its ally; enemy and neutral (map file, owner 255) bombs are walked into. An order to the bomb tile itself is allowed for an own bomb (an ant can bomb itself). |
| Direction | The OPPOSITE of the ant's facing at that moment (`facing + 4`); the facing is the direction of its last step, so an ant that walks north onto a bomb flies south. A hit-knocked ant keeps the facing toward its attacker, so it flies on in the knock direction. |
| Range | Exactly 4 tiles in a straight line (8 directions: 4 columns and 4 rows for a diagonal). |
| Blocked landing | Only the landing tile is tested, never the tiles between: a rock, the map edge, a hill (its 4x4) or the hill's queue and raid tiles refuse it. Then the directions d+1, d+2, ... d+7 are tried in turn (clockwise). Water, an ant, a bomb or a fire wall on the landing tile do not refuse it (a fire wall counts as free ground although it is solid). |
| No free tile at all | No flight: the ant burns on the spot like a dud. |
| Cost | 2 hit points (credited to the bomb owner's team); the bomb is removed (also when it is a dud). An ant at 2 hp dies, at 1 hp after the stun it goes home. |
| Dud | One draw of the match generator (`rand() % 100 < 20`): about 20 percent (201 of the seeds 1 .. 1000). A dud: no flight, the ant stays on the tile under the burn overlay, then stunned. The draw is deterministic given the seed and the draws before it (each `spawn_unit` draws once for the random facing). |
| Chain | A flight that lands on a bomb sets it off at once (each one draws for its own dud) and throws the ant 4 more tiles in the same direction (the flight leaves the facing as it was); 2 hp each. A dud, a landing without a bomb, or a landing that needs a rotation (the direction changes) ends or bends the chain. |
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

## POPcOrN (Community-Maps/POPcOrN.lvl)

Rows 9 and 21 are fire walls across the whole map; rows 10 .. 20 are a field of map bombs (owner 255) with a lake in the middle (row 14 is mostly water: a landing there drowns the ant); the home halves are rows 0 .. 8 (north: teams 0 and 2) and 22 .. 30 (south). A bomb tile in row 5 or lower throws a north-facing ant onto the wall (row 9, 4 rows away), rows 6 .. 8 over it. The real play pinned by the test: the Bomber plants at (3,7) from the lane (3,8), steps north onto it and flies (3,7) to (3,11) (map bomb) to (3,15) (power-up), 4 hp lost; 28 of 40 seeds fly both hops, the rest stop at the first dud.

Reading the tables: `T(x,y)` is the bomb tile, `fN` the facing of the ant on it (`*`: the ant can walk onto T in that facing, the tile behind it is open ground), then the landing of each hop (`.` plain, `B` map bomb, which sets off again, `P` power-up, `~` water, `F` fire wall). Only facings whose first landing reaches the wall (row 9 or beyond; south half: row 21 or beyond) are listed. Each bomb costs 2 hp: a chain of n hops costs 2n (a worker has 10).

The 2-hop chains that end on a power-up (north half): T(3,7) f0, and T(27,7) and T(29,7) in the facings 0, 5, 6, 7 (the last three by the rotation, since the straight landing east or west is outside the map).

### North half

north totals: 198 hops over the wall, 56 onto the wall, 174 over the wall with a chain of 2 or more hops, 19 chains end on a power-up (9 of them after exactly 2 hops)

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

south totals: 196 hops over the wall, 56 onto the wall, 165 over the wall with a chain of 2 or more hops, 16 chains end on a power-up (7 of them after exactly 2 hops)

```text
T(0,22): f1 (0,18)B (0,14)~ | f2 (0,18)B (0,14)~ | f3 (0,18)B (0,14)~ | f4 (0,18)B (0,14)~ | f5 (4,18)B (8,14)~
T(1,22): f1* (1,18)B (1,14)~ | f2* (1,18)B (1,14)~ | f3 (1,18)B (1,14)~ | f4 (1,18)B (1,14)~ | f5 (5,18)B (9,14)~
T(2,22): f1* (2,18)B (2,14)~ | f2* (2,18)B (2,14)~ | f3 (2,18)B (2,14)~ | f4 (2,18)B (2,14)~ | f5 (6,18).
T(3,22): f1* (3,18)B (3,14)~ | f2* (3,18)B (3,14)~ | f3 (3,18)B (3,14)~ | f4 (3,18)B (3,14)~ | f5 (7,18)B (11,14)~
T(7,22): f3 (3,18)B (3,14)~ | f4 (7,18)B (7,14)~ | f5 (11,18)B (15,14)B (19,10)B (23,10)B (27,10)B (27,14)~
T(8,22): f1 (4,18)B (0,14)~ | f2* (4,18)B (0,14)~ | f3 (4,18)B (0,14)~ | f4 (8,18). | f5 (12,18)B (16,14)B (20,10)B (24,10)B (28,10)B (28,14)~
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
T(21,23): f1 (17,19)B (13,15)~ | f2 (17,19)B (13,15)~ | f3* (17,19)B (13,15)~ | f4* (21,19)B (21,15)B (21,11)B (21,7). | f5* (25,19)B (29,15)P
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
