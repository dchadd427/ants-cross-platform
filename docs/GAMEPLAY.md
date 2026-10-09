# How the game plays

This page describes what happens in a match: the data the game reads, the six kinds of ant, how they move, harvest, fight and heal, how a match starts and ends, and what fog of war hides. The rules follow the original game, which was read from its program; a deliberate difference is named where it applies, and each section links to the part of [`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) that holds the ground truth. What you see is in [`VIEW_AND_HUD.md`](VIEW_AND_HUD.md), and what you press is in [`CONTROLS.md`](CONTROLS.md).

## The data files: Direct Binary Asset Pipeline

The game reads the original's own data files directly: no conversion tool and no pre-processing. The code is the library `ants_assets` (`src/ants_assets/`).

- `Original-Ants/ants.chd` is the asset archive: a header, a 256-colour palette, 2,794 raw paletted sprite bitmaps, 91 PCM sound clips and 1,344 animation sequences (section 3 of the reverse-engineering document).
- `Original-Ants/Maps/*.LVL` are the maps: the match length in the header, a tile dictionary, two terrain layers, objects (start markers, plants, food piles, flower-dropper waypoints), the level's default ant type and a final word, every team's egg stock (section 4).
- The map loader reads a file the way the original's loader does (sections 4.4 and 4.5), so maps made by the community load too:
  - the header lists the rows first, so a map need not be square (`OCEAN` has 81 rows of 100 columns);
  - bytes after the final word are never read;
  - the final word is every team's egg stock as it stands (the community editor's template ends with 32766);
  - block 3 is the level's default ant type: on `POPcOrN` every ant is a combat ant, on `Bombz Away` a bomber (see Ants and their types).
- The archive stores each directional animation in five of the eight directions. The other three are mirrored copies of the sprites, made once when `ants.chd` loads, so a direction is an O(1) table lookup (section 5.8).

Ground truth: [`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) sections 3, 4 and 5.8. The sprites, sounds and animations of `ants.chd` can be browsed in the asset catalog ([`ASSET_CATALOG.md`](ASSET_CATALOG.md)). What comes from the original game is explained in [`ORIGINAL_PROGRAM.md`](ORIGINAL_PROGRAM.md); which of its files the repository holds, and on what terms, is under [License](../README.md#license) in the README.

## Deterministic 20Hz Simulation Engine

`ants_sim` (`src/ants_sim/`) is the deterministic 20Hz simulation engine. It runs in discrete ticks of 50 ms with the original's timing and movement rules. The same map, roster, random seed and commands always give the same match, which is what lets a network match run in lock-step ([`MULTIPLAYER.md`](MULTIPLAYER.md)).

The match length is read from the map's header (a map that says 0 minutes gets 12). The six maps that ship with the game:

| Map | Size | Match | Ants per team at the start | Eggs per team | Food piles / total points | Power-ups at the start | Flower droppers |
|---|---|---|---|---|---|---|---|
| `TINY` | 31 x 31 | 6 min | 3 | 3 | 14 / 4800 | 0 | none |
| `SMALL` | 40 x 40 | 8 min | 4 | 2 | 5 / 4000 | 2 | 2 |
| `MEDIUM` | 60 x 60 | 10 min | 6 | 6 | 4 / 4900 | 0 | 1 |
| `GAUNTLET` | 60 x 60 | 10 min | 6 | 6 | 2 / 1800 | 10 | 1 |
| `ISLANDS` | 60 x 60 | 12 min | 8 | 4 | 10 / 5600 | 40 | 2 |
| `TREASURE` | 60 x 60 | 12 min | 6 | 9 | 18 / 10950 | 20 | none |

Ground truth for the match length: section 5.27. How a bot sees these numbers is measured in [`BOTS.md`](BOTS.md#what-the-game-looks-like-to-a-bot-measured).

## Ants and their types

There are six types of ant. Every ant has 10 hit points, can harvest and carry food, and walks at the same pace as every other. Ants start as workers, and a power-up changes the type of the ant that takes it.

| Type | Power-up tile | What it adds |
|---|---|---|
| Worker | none | nothing: the plain ant that hatches from an egg |
| Combat | 62 | a stronger blow (2 hit points, a 4-tile knock-back) and the auto-engage reflex |
| Thief | 63 | raids an enemy anthill |
| Bomber | 64 | plants and defuses bombs |
| Swimmer | 65 | swims, builds and demolishes bridges |
| Fire | 66 | lights and puts out fire walls, and walks through them |

Each type has its own clips, voices and panel text.

**A level's default ant type.** A level may make every ant one of the types. Block 3 of the `.LVL` file names a power-up tile as the level's default ant type, and the original's type getter then answers it for every ant that never took a power-up. On such a community map (99 of the 540 that load, none of the six shipped maps) the ants the match starts with and the ants that hatch are all of that type, for all four teams, with that type's clips, voices, panel and abilities. There are three exceptions, because those places read the ant's own type and not the default:

- a default Thief cannot raid (a click on an enemy hill stops it);
- a default-type ant drops no power-up when it takes one, and none when it dies;
- the larger click box of a combat ant belongs to an ant that took the Combat power-up only.

Ground truth: [`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) section 5.62. The bots' pool on such a map is in [`BOTS.md`](BOTS.md#what-the-game-looks-like-to-a-bot-measured) ("Typed ants").

## Frame-Exact Movement & Pathfinding

There are no speed constants. An ant moves by the displacement of its current walk frame when that frame ends, on a millisecond clock, so the walk clip is the speed. Every ant type walks at the same pace (a tile is 32 px):

| Ground | Step per frame | Frame | One tile straight on |
|---|---|---|---|
| Grass | 4 px | 50 ms | 400 ms |
| Sand | 4 px | 40 ms | 320 ms |
| Dirt | 4 px | 60 ms | 480 ms |
| Mud | 2 px | 60 ms | 900 ms |
| Swimming | 3 px | 40 ms | 400 ms |

A bridge, at any stage of its building, walks like mud.

- Paths come from a port of the original's asynchronous `PATHMGR` A*: one 1000-expansion slice per 50 ms for each team, 8 directions.
- An order snaps the ant to the centre of its tile, like the original's `GoTo`.
- A walker waits 300 ms behind a moving ant, and re-plans around a standing one.

Ground truth: [`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) sections 5.28 and 5.32.

## Food Objects & Harvest

- A food pile is an object of the map (LVL block 2): its anchor, its units, the points of one unit and a list of stage tiles.
- The cells of a stage are the tiles that its picture covers (up to 19 on the shipped maps; for example 2 x 2 for the crackers and 4 x 4 for the burger). They are solid for walkers, so paths avoid them.
- An ant harvests only when it is ordered onto the pile. It walks up to it and plays the `?gf` grab clip (action 5).
- When the clip ends, one unit is taken (the pile shows its next stage or disappears). The ant carries the points of one unit, "Got Food!" is posted and the ant heads for its hill. After the deposit it walks back to the pile.
- Two ants that begin on the last unit both get food.
- An ant that already carries food answers "Can't - already have food.".
- A lunchbox dropped by a dead carrier is a food object of one unit.
- Food is finite: nothing respawns.

Ground truth: [`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) section 5.40.

## Power-Up Lifecycle & Droppers

### Power-ups

- A power-up is its tile id (62 to 66), never its dictionary name, and it is drawn the same on every map: community maps whose dictionary calls those entries `.` have all their power-ups, as in the original.
- An ant takes a power-up only when its walk ends on the power-up's tile, never from a distance.
- The type changes at once, and the `getpow` clip (action 4, 840 ms) plays with sounds 1 and 2. Hit points stay.
- An ant that already had a type drops its old power-up on a free neighbour tile (the West neighbour is twice as likely as each of the others; with no free tile the old power-up is lost).
- An order given while the ant is crossing into the tile cancels the pick-up: the ant stands on the power-up and cannot be attacked.
- Power-ups are solid obstacles for paths, attacks and knock-backs.

Ground truth: [`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) sections 5.38 and 5.62.

### Flower droppers

- Daisy flowers (`flower1`, animation 421) on maps such as `SMALL.LVL` and `GAUNTLET.LVL` drop power-ups. A drop is a 9-frame falling droplet animation (`FD_*`: `FD_COMB`, `FD_SWIM`, `FD_THIEF`, `FD_FIRE` or `FD_BOMB`) with sound 62 (`powerdrip.wav`). After 820 ms the power-up lies on the ground tile in front of the flower.
- The interval and the odds come from block 4 of the map: waypoints with five probabilities (bomber, combat, thief, swimmer, fire). On the shipped maps a class with probability 0 is never drawn; if the five probabilities of a map add up to less than 1, a draw above their total picks any of the five classes at random.
- A dropper is a plant by the tile flag of its id (the clovers and flowers, whatever the dictionary calls them) with the first block 4 record at its cell when that record's flag is not 0.
- The dropper task polls about every 3 s, so an interval is rounded up to a whole poll: the 8 s of `MEDIUM` is 9 s in play.

| Map | Droppers | Interval in the file | What falls |
|---|---|---|---|
| `SMALL` | 2 | 15 s | bomber 45 %, swimmer 10 %, fire 45 % |
| `MEDIUM` | 1 | 8 s | bomber, combat, thief and fire 25 % each |
| `GAUNTLET` | 1 | 30 s | bomber 10 %, combat 40 %, swimmer 10 %, fire 40 % |
| `ISLANDS` | 2 | 60 s | bomber 5 %, thief 20 %, swimmer 70 %, fire 5 % |
| `TINY`, `TREASURE` | none | | |

Ground truth: [`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) sections 5.29 and 5.62.

## Special Abilities

A special order plays the original's action clip, with its frame sounds. A melee hit or a stun cancels an ability. A refused order plays the can't clip with "Can't do that...". There is no cooldown. Bombs and fire walls go on empty grass, sand or dirt: never on mud, water, a bridge or the hill.

### Bomber

- Plants a bomb (about 1.4 s). The tile holds an invisible solid placeholder until the clip ends.
- Defuses a bomb of any team (about 1.1 s).
- A bomb goes off when an ant walks onto it: the ant loses 2 hit points and is thrown 4 tiles. One bomb in five is a dud (see Death & Burning).
- With several ants selected, the move click sends them all to the bomb, and the first one to step on it sets it off.

### Fire

- Lights a fire wall (about 1.8 s) and puts it out (about 1.2 s).
- A wall lives 180 s while more than 180 s of match time remain.
- Other ants cannot walk into a fire wall, and one that is thrown onto it loses a hit point. A fire ant walks through it; thrown onto one, it is stunned instead.

### Swimmer

- Swims in water. For every other ant water is not walkable, and one that lands in it drowns.
- Builds a bridge on a water tile in three passes of about 0.5 s (the first of the bridge's four stages is laid at once) and demolishes a finished one in four passes.
- A finished bridge collapses after 180 s while more than 180 s of match time remain.
- When a bridge is destroyed, by a demolish or by the timer, the shared bridge-collapse scan drowns every non-swimmer on its tile. A swimmer only splashes.

### Thief

- Raids an enemy anthill: the clip `atcr501` lasts 3.5 s. When it ends, the thief takes up to 50 points from the hill's team and carries them home like food.
- Sound 48 (`anthill.wav`) sounds the alarm for the raided team.
- A raid on an ally's hill is stopped.
- A thief that is killed drops the loot it carries as a lunchbox.

Ground truth: [`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) sections 5.37 and 5.35 (the raid).

## Combat & Knock-back

- Contact is the attacker's step into the target tile. There is no range and no cooldown, and each order gives one blow.
- Hit points are lost at contact: 1, or 2 from a combat ant.
- At the strike frame the victim is thrown: one tile with the `gh` clip, or four tiles with the combat ant's `gb`.
- A thrown ant lands by the original's block order:
  - a bomb goes off;
  - on a pile-up the ants on the tile are thrown to free neighbour tiles (the dust cloud shows for ants that are not yours);
  - a fire wall costs a hit point, or stuns a fire ant (no dust cloud);
  - water drowns a non-swimmer, and a swimmer splashes and is stunned.
- Death is deferred until the death clip ends.
- The combat ant's only AI is the original's auto-engage: an idle combat ant attacks an enemy close by, then goes back to what it was doing. There is no guard post and no pursuit.
- An ant that a hit leaves with 1 hit point walks home by itself to heal.

Ground truth: [`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) sections 5.36 and 5.39.

## Anthill Enter, Heal, Hatch & Raid

- An ant that reaches its hill's entrance plays the original's enter clip: `?h0`, or `h?h0` when it carries food (about 1 s).
- Its food scores and its health is restored when the clip ends. The heal frame of a wounded ant's clip lasts `(10 - hp) * 200` ms instead of 40 ms.
- Ants wait on a ring in front of the hill until the waiting-queue task (ANTHILLQ, every 200 ms) admits them one by one.
- A click on the hatch pedestal costs 200 points (it is refused with fewer), uses one egg of the team's stock and starts an 8 s incubation. Only one egg incubates at a time.
- After 8 s a worker appears at the entrance (later, while one of your ants stands on it) and plays `aghatch` with sound 43 (`exithill.wav`). On a level with a default ant type the newborn has that type.
- When a team's last ant is removed and it still has eggs, one hatches at once, even with fewer than 200 points (it still costs up to 200).
- A thief's raid is described under Thief.

Ground truth: [`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) section 5.35. The names `exithill.wav` and `anthill.wav` are the remake's labels for sounds 43 and 48: the archive stores no file name for them.

## The start of a match

Every match opens with the original's dialog: its picture, `Get ready to play!  You are the Green Ants.` (Green, Red, Blue or Black: your team's colour), the worker, "Waiting for others...", the 5 s of its task KWFO. It takes every click and key.

### The "Get ready to play!" dialog does not cost match time (a deliberate deviation)

- The original already runs the match clock and the ants behind the dialog, so in a game of one person you lose five seconds of the match (in a network game its clock starts at GO, so you lose less, or nothing).
- Here the simulation waits: tick 0 runs when the dialog closes, and the clock shows the match's full time while the dialog is up.
- The ants stand in the picture behind the dialog, locked until it closes. The original creates them when its GO message is handled, so it shows none while it waits for the others.
- A game that starts straight into a match (`--map`) has no dialog.
- In a network match every machine closes the dialog when its first turn runs ([`MULTIPLAYER.md`](MULTIPLAYER.md), "How a match runs").

Ground truth: [`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) section 6.2, item 21 ("Match Start 'Get Ready!' Modal ..."). The deviation is listed in [`AUDIT_ONE_TO_ONE.md`](AUDIT_ONE_TO_ONE.md#3b-deliberate-differences-requested-by-the-owner-tweaks).

## Death & Burning

- A dying ant plays `death1` to `death4` on its own sprite until it is removed. Meanwhile it keeps its hit-point number (Ctrl+L) and its minimap dot.
- A dead ant drops the food it carries as a lunchbox when it dies on free land (one that drowns loses it), and a typed ant drops its power-up on a free neighbour tile.
- One bomb in five is a dud. The ant that set it off burns where it stands, frozen under the flames of the `?bu` clip, and is stunned when they end.
- The flames are a sprite of the view container: they are drawn over every ant and hidden only on unexplored ground.

Ground truth: [`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) sections 5.36 and 5.60.

## Seeing the enemy

### Enemy Ant Inspection

Clicking an enemy ant when none of your ants is selected selects it for inspection: its selection brackets (`*ears`, coloured by health) show, but no order can be given to it. The click itself is in the mouse table of [`CONTROLS.md`](CONTROLS.md).

The brackets are `dogears` (green) at 9 or 10 hit points, `yelears` (yellow) at 3 to 8 and `redears` (red) at 1 or 2. Your own selected ants get the same ones.

### Fog of War

Fog of war is an option of the setup screen. Ground that none of your ants has seen is covered by a dithered veil, and what has been seen stays explored for the rest of the match. A tile that is not explored hides the enemy ants, the food, the bombs, the fire walls, the power-ups and the effects on it.

- The ground is uncovered the way the original does it: a 13 x 13 square around an ant whenever the position of one of your ants or your teammate's is updated. Nothing is uncovered around a hill, and nothing when an alliance forms (an ally's ants uncover ground when they next move).
- The ground's objects are drawn from three cells beyond the view.
- An object whose anchor is unexplored is drawn again from every explored cell of its footprint (the original's second path of the layer-2 pass).
- Food is hidden only while neither its anchor nor a cell of its current stage's footprint is explored.

Ground truth: [`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) sections 5.54 and 5.57.

## End of the Match

The match ends:

- when the clock runs out (checked every 200 ms, like the original's CHECKGO task);
- when no team has an egg, a hatch or an ant left;
- when the teams that still have something are one alliance whose combined score is strictly the best (a tie is never a win);
- when a drop-out leaves one team or an allied pair alone;
- when a player quits while exactly one other side is left (the quitter's row goes last on the results). With more sides left, a quit is a drop-out.

The score is the food your ants have deposited at your hill, plus the loot your thieves have brought home, minus the loot that thieves took from you and minus the 200 points that each hatched egg costs. When the clock runs out, the results rank the teams by score; an alliance's two scores are added, and the top row wins. The results screen is described in [`VIEW_AND_HUD.md`](VIEW_AND_HUD.md).

Ground truth: [`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) sections 5.47 (the end rules) and 5.49 (the results and their ranking).

## 187 (a game type of the remake)

The room's **game type** is either *Highest score* (the original's game, everything above) or **187**, the remake's own: a fight with no food. The type is chosen with the room, is the same on every machine and is written into the replay (`GameMode`, `include/ants_sim/game_mode.hpp`). A match that does not ask for it plays exactly as before and keeps its state hash.

In 187, on any map:

- **No food and no power-ups.** Every food pile and power-up of the map is taken off when the match starts and the flower droppers are switched off (the plants stay as scenery). Nothing can turn an ant into another type, so the ants are the ones the map starts with.
- **The start is the map's own.** The start markers, the starting ants and the starting eggs are those of the map, as in any match. The eggs are the lives of a team: a team that loses its last ant hatches one egg at once, as in the original, but the hatch costs 187 no points (the score is the kills and the hatch must not eat them). A player cannot hatch an egg by hand: that needs 200 points as always, and a score of kills never gets there.
- **The score is the kills.** Every enemy ant that a team kills is one point, shown in the score boxes and added across an alliance as always. The kill goes to the team whose hit was the last on the ant, as the original counts "Enemy Ants Killed" (a push into the water counts); an ally's bomb or fire is no kill, and the ants of a team that dropped out die for nobody. Nothing else scores, and a kill makes no bubble and no cue (a fight has many).
- **The match ends** when the clock runs out, when nobody has an ant left, or when **one side is left**: every other side has no ant (no egg and no hatch either), and the last side standing wins outright, whatever the kill counts are. An alliance whose two teams are both alive is one side. A match of one team has nobody to beat and does not end by this rule.
- **The results** rank the side that is left first, then the rest by kills (an alliance's rows are added; a tie goes to the team of the screen, as in the original); the winner cue plays for the first row's team and its ally. The ants that each team has left are counted for the results page.
- **The chat log** starts with "Game started! Most kills wins." (the original's "Game started! Go get that food!" belongs to the other type). When a team of the match has no ant, no egg and no hatch left, every player reads "<Name> (<Colour>) is out of ants!" in the chat log, once, and hears the cue of a player who drops out; a team that dropped out has its own line and gets none, and a team that is not in the match is never named.
- **The results page** keeps the rows, "Winner!", "other players..." and the Leave Game button as they are, and changes what is written on it: three headings, *Kills*, *Ants lost* and *Ants left*, each with an arrow over its column, replace the four of the original (the "new ants hatched" column is not shown), and a line at the top says how the match ended: "<Name> is the last colony standing!" (two allies that both have ants: "<A> and <B> are the last colonies standing!"), "Time is up. The most kills wins!" when the clock ended it, or "Nobody is left. The most kills wins!". A match that a player's quit ended has no such line.

The original ends a match by the best score and never lets a score of 0 win, which is why 187 has its own end rule. Code: `SimulationEngine::set_game_mode`, `SimulationEngineImpl::end_rule_187`, `MatchResult::standing`; the chat lines: `SimulationEngineImpl::announce_out_of_ants_187`, `HUD::set_game_mode`; the page: `ScorecardModal` and `results_layout.hpp` (`draw_results_headings_187`, `fit_results_headline_187`); tests: suites 2.9.2 (`tests/test_sim/test_game_mode.cpp`), 2.9.3 (`tests/test_sim/test_out_of_ants.cpp`) and 3.16.2 (`tests/test_app/test_results_187.cpp`). The mode is hashed (only when it is not the original's), so a replay of a 187 match is refused by a build that does not know it (docs/REPLAYS.md).

## Ant Animation in Real Time

Every clip that an ant plays steps at the moment a frame ends, as the original's real-time player does: walking on every terrain, standing, swimming, diving, climbing, harvesting, can't-go and all the actions.

- The simulation runs in 50 ms ticks. The frames and the displacements that end between two ticks are shown in advance: sand shows its 40 ms steps smoothly, mud its 60 ms steps, and the worker's idle clip its own 150 / 75 ms.
- The selection ears and the hit-point number follow the sprite.
- A frozen ant shows no number.

Ground truth: [`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) section 5.59.
