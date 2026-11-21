# Computer players that are stuck, and what they do about it

Two matches of computer players showed ants that stood for minutes and said "Can't go there." again and again. In the first (a map of the library, four hills, two fire walls and a field of map bombs between the halves) a Thief of one team raided a hill in the other half, took 50 points and stood on the raid tile for the remaining ten minutes with the loot. In the second (a map of the library with four quarters, each shut off from the next by a wall whose gaps are filled with power-ups) Thieves of every team did the same, and the ants that the map had dealt into the other quarters stood still. This page says what the engine does there, which rules the standard bot ([`BOTS.md`](BOTS.md#the-standard-bot)) plays now, how a stuck ant is counted, and what the rules measured.

## What the engine does

- **The order that puts a Thief into the can't-go loop is the engine's own, not a bot's.** At the end of the raid clip a Thief that holds loot is ordered to its own entrance (`raid_clip_end`, the original's `0x101efbd`; the recorded bot gave no order after turn 4321, the raid order, and the loop began at turn 4430). When no walk leads there the walk is refused and the can't-go clip plays (18 ticks for a Thief); at its end an ant that stands on a special tile of a hill (the queue row, the ramp, the entrance, the raid tile; of any hill) is ordered to its own hill's waiting tile (`GoTo(+0x42)`, `0x101f190`: [`BOTS.md`](BOTS.md#the-cant-go-loop); `docs/GAME_REVERSE_ENGINEERING.md`, 5.40, "After the clip"; the steps of the actions 5 and 0xd in [`docs/reverse_engineering/movement/03_walk_step_arrival_and_stop.md`](reverse_engineering/movement/03_walk_step_arrival_and_stop.md)), which is refused again. The remake does what those pages say; there is no difference to report (the program itself is not in this checkout, so nothing was read from it again). An ant in the clip is not one that takes an order (`can_take_user_order` accepts the actions 0, 1 and 3, the clip is action 0x0B), so what decides whether an order can get in is the refused walk: when the failed search is bigger than one slice of the path manager (a walk over a big map) the ant is Walking for the one tick in 19 in which the search is pending, and an order applied on that tick frees it (AI57.1), which a bot cannot aim at; when the failed search is answered at once (a small region: the north half of POPcOrN) the ant is in the clip at every tick and no order of anybody gets in (the recording played on with an order at every tick for 400 ticks: none taken). Nothing but a walk home that opens ends such a loop. A Thief steals whether or not it can get home (a raid takes up to 50 points from the hill it raids whatever happens to the Thief afterwards).
- **A power-up tile is solid.** No walk crosses it (rule R4 of `can_enter`); the one order that may name it is the pick-up, and the ant that arrives takes it and becomes the Thief, Combat Ant, Bomber, Fire Ant or Swimmer that it makes. A power-up in a gap of a wall, a corridor or a doorway therefore shuts it like a rock until some ant takes it. The analysis of the bots (`MapInfo::walkable`) already read it so: the walkable parts of such a map are separate, and so are the ants in them.
- **The way over a power-up without taking it** exists in the original and in the remake: an order given to another tile inside the cancel window of a pick-up (the 200 ms between the crossing of the tile's edge and the landing) snaps the ant onto the tile and leaves without the pick-up (`tests/test_sim/test_powerup_actions.cpp`, 1.3, 1.5), and the tunnel hop of the corner rows of ISLANDS is a chain of such orders (1.14); an ant that stands on a power-up and is clicked onto its own tile takes it (1.7, 1.18). The bots do not use it for a plug: see "What is left".

## The rules

All of them are plan switches that ship on at every level and that `bot_arena --tune` turns off for a comparison (`plugs=0`, `refmem=0`, `rescue=0`; `unstick=0` is all three off, the bot as it was).

- **A power-up that plugs the way is taken by one ant** (`PlugTask`, `LevelPlan::plugs`). Every 100 ticks (from tick 60) the task labels the walkable parts of the map as it is now, finds the power-ups that touch two of them one of which is the part of the own hill, and when the other part holds an own ant or a pile with food sends one ant to take the power-up: the ant of the far part first (it was stranded, with nothing else to do; walks of up to 1,600 ticks), else the nearest empty-handed ant of the hill's part that no task of a higher rank holds (up to 500 ticks). Only an ant of the level's default type is sent (a typed ant would lose its type), and a power-up that opens nothing for the seat (no own ant, no food behind it), one that another ant stands on, and one that the trip task has an ant on its way to are left alone. The ant keeps the type it takes; the way is open for everybody.
- **A Fire Ant puts out the wall that shuts in an own Thief** (`LevelPlan::rescue_thief`, `shuts_in_own_thief`). A wall in front of the hole of another team's hill, with an own Thief in the can't-go clip on that hill's raid tile, is counted as harm to the economy like the walls in front of the own gate, so the wall task sends its Fire Ant to put it out (the counters of [`BOTS.md`](BOTS.md#fire-play-contest-batch)), and the plan wants a Fire Ant while such a Thief stands, which the trip task fetches from the Fire power-up of the own side. When the wall is out the engine's own walk home takes the Thief out with its loot.
- **A click that the engine refused twice is not given a third time for a while** (`RefusalMemory`, `LevelPlan::refusal_memory`). An order that left and was not carried out (the ant stands on the tile it stood on, idle or in the can't-go clip, 40 ticks later, and no path is on its way) is a refusal of that kind of click by that ant onto that tile. The first refusal blocks nothing (a team-mate on the doorway, a bomb that goes off, a wall that burns out make a click work the next time); the second blocks it for 600 ticks, the third for 1,200, and so on to 4,800. The orders of a look are looked over before they leave and the blocked ants are taken out of them; the tasks are told as for a click that the controller refuses (`Fate::Filtered`), so none proposes it again at every look. Carriers, clicks on or beside the ant's own tile and clicks into a mound (the walk home ends in the hill's queue, which is no refusal) are never judged, and a timed chain is never touched.

## What is left

- **A Thief that is shut in by walls** on the raid tile of a hole whose team lights the three tiles in front of it after the raid stays in the loop until a wall burns out or a wall is put out (the walls of a thief hole closing behind a Thief are what they are for, [`BOTS.md`](BOTS.md#the-cant-go-loop)): the Fire Ant of the Thief's team is sent to put one out, see "The rules".
- **The pass over a plug without taking it** (the cancel window above) would keep the type of a Thief that crosses. It is not built: the plug is opened once for everybody by the ant that takes it, which costs one worker its plain type and no more, and the timed chain it needs (a window of two ticks inside one of three) is the most fragile order of the bots.
- **A Thief that raids a hole it cannot get home from, on a small region** (the first match: a hole behind two fire walls and a field of map bombs, with the Fire power-ups of that map behind the bombs) stays in the loop for the rest of the match: no order gets in and nothing opens the way. Thieves steal no matter what; on a map whose gaps are plugged the opened plugs are what lets such a raid bank.
- **Ants that the map deals behind a wall of fire and bombs** have no power-up of the right kind on their side to break through with; they stand where they are.

## The counter

`bot_arena` counts the ants that stand nowhere (`StuckTally`, `ArenaResult::stuck`; the counter changes nothing in a match): an own ant is stuck when for 10 seconds in a row it has stood on one tile and was in a state that goes nowhere, which is the can't-go clip (*looping*), an order that does not move it (*blocked*) or food or loot in its hands with no order, more than 8 tiles from its own mound (*carriers*: the engine's walk home that ended at once, which the economy sends home after 900 ticks, [`BOTS.md`](BOTS.md#the-standard-bot); the carriers that wait for the gate stand about the mound, a wait of 700 ticks is legitimate, and are not counted). An ant in a clip of its own work (a raid, a plant, a fire wall, a pick-up, a bite, the hill's entrance) or queueing for its hill is not. The report (format 8) has per seat `stuck` (`ants`, `ticks`, `looping`, `blocked`, `carriers`) and `refused_again` (refused orders that give a first ant the same kind of order onto the same tile as an order that was refused before); a run prints the totals, and `--stuck-detail` prints one line for each of the first stuck spans of a match.

## Measured

`bot_arena`, seeds 1 to 8, four bots of one kind on the four seats (Hard, Medium, Hard, Medium), ten maps (the six of the original, POPcOrN, HELLRAISER, Bombz Away and the map with plugged gaps; 320 seat-matches). "Before" is `--tune unstick=0`.

| | before | after |
|---|---|---|
| stuck spans (10 s) per seat and match | 0.41 | 0.36 |
| of them looping in the can't-go clip | 0.19 | 0.18 |
| ant-minutes stuck per match | 6.7 | 5.6 |
| can't-go reactions per seat-minute | 5.38 | 4.05 |
| refused orders per 1,000 orders | 41.6 | 29.4 |
| refused orders that repeat an earlier refusal, per seat and match | 4.78 | 2.67 |
| mean score per seat | 931 | 930 |
| steals completed per seat and match (points banked) | 5.57 (275) | 5.61 (278) |
| power-ups taken per seat and match | 3.76 | 3.84 |

By map: the map with plugged gaps has 1.22 stuck spans a seat and match before and 0.69 after (looping 0.78 to 0.47, can't-go reactions 241 to 108, refused orders 96 to 63 in 1,000), POPcOrN 48 to 25 reactions, MEDIUM 0.19 to 0.03 spans, ISLANDS 17.9 to 8.2 refused orders in 1,000, TREASURE 81 to 50 reactions; TINY, SMALL, GAUNTLET and Bombz Away play as before. HELLRAISER and the loops of POPcOrN do not change: the Thieves there raid into holes they cannot get home from, and nothing opens the way (see "What is left"). 89 to 99 percent of the raids that begin are completed on the maps where Thieves can get home.

**One rule at a time**, two bots with the rule against two without it in the same match (Hard, every arrangement, seeds 1 to 4, six maps, 144 matches each; `--seat N=standard+plugs=1,...`, both sides the level's neutral plan): mean score a seat, with and without; matches won by a seat with the rule against one without (ties left out):

| rule | with | without | won |
|---|---|---|---|
| `plugs` | 1,071 | 1,050 | 72 : 66 (the map with plugged gaps 1,259 : 1,131, 15 : 9) |
| `refmem` | 1,059 | 1,054 | 68 : 65 |
| `rescue` | 1,056 | 1,056 | 69 : 69 (TREASURE 2,090 : 2,072) |

A fourth rule, that a Thief raids only where a walk leads home, was built and removed: it kept the Thief out of the loop but cost 94 points a seat (1,011 : 1,105) because a raid that cannot bank still takes up to 50 points from the hill it raids; a Thief steals whatever happens to it afterwards.

**The three rules together**, two bots with them against two without, in the same match (every arrangement, seeds 1 to 4, ten maps, 240 matches each; `--seat N=standard+styled=1:LEVEL` for the two and `standard+styled=1,plugs=0,refmem=0,rescue=0:LEVEL` for the other two: a seat with `standard+K=V` and no `styled=1` plays the level's neutral plan with no style, which is another bot):

| level | score with / without | matches won | stuck spans a seat and match | can't-go reactions a seat |
|---|---|---|---|---|
| Hard | 956 / 882 | 113 : 106 (21 ties) | 0.35 : 0.35 | 66 : 66 |
| Medium | 993 / 930 | 111 : 110 (19 ties) | 0.43 : 0.45 | 59 : 68 |

The map with plugged gaps gives most of the score difference (Hard 1,486 : 883, Medium 1,495 : 897); the other maps score alike. The rules do not free a Thief that stands in the loop on a small region (POPcOrN, HELLRAISER): they keep the bots from asking again for what the engine refuses, open plugged ways, and put out the walls that shut in a Thief.

## Tests

AI57.1 to AI57.9 in `tests/test_ai/test_ai_unstick.cpp`; the mutants are `tests/data/bot_unstick_mutants.json` (`python3 tools/mutate.py tests/data/bot_unstick_mutants.json --keep-going --jobs 1`).
