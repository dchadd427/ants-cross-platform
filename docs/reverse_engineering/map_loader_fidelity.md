# The map loader as the original's: what v0.12.10 changed

Where the remake's level loader (`Grid::init_from_level`, the level reader, the renderer's copy of the hill search) decided by a dictionary NAME, by a guess or by its own rule, it now does what the original does, read from the program (the tile-info tables, the loader `FUN_01006349` and its helpers, `SetTile` `FUN_01007352`). The original was read, never run. Each fix is its own commit with its own test (`tests/test_sim/test_loader_fidelity.cpp`, suite 2.9.4) and was swept over the 558 community maps (`tools/map_sweep`, 2,400 ticks, two rosters, three runs each: no crash, no hang, every run deterministic).

## Numbers

Every fix below changes how a level is read, so by the rule next to `kProtocolVersion` the network protocol is 19 (nothing on the wire changed) and the replay rules numbers are 5 (the original's game) and 6 (game mode 187); recordings of rules 1 to 4 are an earlier version's ([REPLAYS.md](../REPLAYS.md#the-file)). One bump covers all the fixes.

## 1. Power-ups, food, fire walls and lunchboxes are solid when the file's solid bit says so

**The original.** A cell blocks by its layer-1 solid bit (`FUN_0100cf0f`), which the loader copies from the file (`FUN_010069d8`). A power-up, a food pile, a fire wall or a lunchbox is not solid by being one: `SetTile` (`FUN_01007352` -> `FUN_01007a22`) copies the tile flag 0x01 of the id to the bit when it places the object, and clearing the cell (`FUN_0100744f` -> `FUN_0100660c(row, col, 0)`) clears it again, row 0 never. A power-up that a file leaves on a cell without the bit is ordinary ground: paths cross it, ants stand on it, nothing happens (a power-up is taken only by the arrival of a move or power-up order whose tile it is). The exceptions of `CanEnter` and of the path step cost (the power-up order onto that tile, a harvest, a fire ant on fire) are evaluated only inside the `solid != 0` branch.

**The remake did.** `Grid::is_solid_object` counted every power-up, pile, lunchbox, fire wall and placeholder as solid by itself, and `init_from_level` cleared their file bits. `POPcOrN.lvl` (all 32 power-ups without the bit) therefore had its corridors plugged.

**Now.** The file's bit is kept and is the only source for a level read from a file. What the game places sets it and what it removes clears it, as `SetTile` does: `place_powerup`, `place_firewall`, `set_fire_at`, the flower dropper's power-up, `set_food_tile` (the footprint of every stage tile, so also the lunchbox), `set_layer2` (the placeholder 160, bridge stages, empty) and `place_bomb` (replacing the placeholder). A world made by hand (`init_empty`: tests and tools) has no file bits and keeps counting these objects as solid themselves. The cells under a pile's footprint also stop being power-ups (they kept the flag while holding the pile's tile).

**Maps.** Power-ups without the bit: 3,132 cells in 94 maps; food: 183 cells in 36; fire walls: 439 in 2 (`LF1.6`). Sweep of the 558 maps (loaded state, and 2,400 ticks with the sweep's scripted players, rosters 15 and 9): the walkability of the map at load changes in 107 maps; the play changes in 101 (the state hash of 451 others only because it now holds the bit that was cleared before; the six shipped maps play exactly as before, `LF1.7`); no crash, no hang, every run deterministic.

## 2. Hills and start markers are tile ids

**The original.** The hill of a colour is the layer-2 id 245 + colour (245 black, 246 blue, 247 red, 248 green; `FUN_0100ecac`, `FUN_0100ecdf`), the start marker of a colour the block-1 id of the table at 0x1002468 (152 black, 153 blue, 154 green, 155 red; `FUN_0100ec18`). The dictionary is never asked (the four hill ids and the four markers are in the table of forced graphics). Block 1 starts an ant for a record only when its id is one of the four markers of the team's colour.

**The remake did.** The level reader named a record's team by substrings of its dictionary entry (`GSTART`, `GREENHILL`, `ghill` ...), and `Grid::init_from_level`, `LevelData::for_roster`, the renderer and `map_sweep` found the hills by the names `greenhill` ... `blackhill`. A damaged entry lost a hill or a team's start ants, a permuted dictionary swapped the colours of hills.

**Now.** `ants::assets::hill_team_of_id` and `start_marker_team_of_id` (`lvl_parser.hpp`) are the one place that knows the ids; the reader, the grid (hill origins, the hill cells' overlay rule), `for_roster` (the erased hill of a colour nobody plays), the renderer (hill art and anchor, what the static pass skips) and `map_sweep` use them. A block-1 record that is not one of the four markers has team 255 (a hill-named record in block 1 starts no ant any more).

**Maps** (`LF2.4`): 7 maps change by hills (CRISS CROSS, Conquer, LIFELESS, X-pRtfight lost a hill to a damaged entry; NO LIMIT, TREZ OF COMBAT, TREZ OF LIFE had colours swapped by a permuted one), 6 by start markers (BOMBERWORLD, CRISS CROSS, Conquer, NO LIMIT, TREZ OF COMBAT, TREZ OF LIFE): 8 maps in all, all of which play differently; none of the other 550 changes in any way (state hash and 2,400-tick play identical). CRISS CROSS and Conquer have four teams now (`players` 4 in `Community-Maps/maps.json`). Their pictures change (`test_loader_fidelity_render`): the hills of those 7 maps.

## 3. The terrain class of a layer-1 cell is the original's table by tile id

**The original.** `FUN_01008af7` takes the class of a cell (0 grass, 1 sand, 2 water, 3 mud, 4 dirt; a bridge on layer 2 makes it mud) from the table at 0x1001360 indexed by the layer-1 tile id (the remake carries the same 1,344 entries in `movement_tables_data.inc`). The dictionary's name of the entry and every flag bit of the cell but the solid bit (the loader keeps bit 0 of the cell word and drops the rest, `FUN_010069d8`) play no part. The ids 1 and 2 are grass.

**The remake did.** `Grid::init_from_level` applied the table only to a tile whose dictionary name was not ".". A lake, a mud path or a dirt road that the editor left unnamed was grass (ants walked across a lake), a cell with the flag 0x100 was water, the id 2 water and the id 1 a wall, all named ".".

**Now.** The class is the table's, by id, for every layer-1 cell (`determine_terrain_type` is gone).

**Maps** (`LF3.5`): 16 maps. Water 476 left unnamed: 1,921 cells in 2 maps (Wata fight, hell2: ants crossed a lake of over 900 cells); mud 435 .. 444 in the Gant2 family (Gant2, gant 2, gauntlet2) and eze jungal; dirt 559 in the Turtle World family (!TurtleWorld, #Turtle World, Turtle World, Turtle); the flag 0x100 on unnamed tiles in IsLaNd FiRe BoOm (701 cells) and in Sand Box, SandBox and sand-box (1 each); and, found by the same rule, the id 1 (a wall in Bloom) and 2 (water in 's 4 Player Royal), one cell each. Sweep: the loaded terrain changes in these 16 maps and the play (2,400 ticks) in 11 of them (the other 5 only in the hash); no other map changes; no crash, no hang, every run deterministic. The pictures of the 16 maps change (the minimap and the map picture colour the cells by class).

## 4. The block-1 records that the original ignores

**The original.** Block 1 is a list of (tile id, row, column). The match screen (`FUN_0100dbb0`) starts an ant for each record whose id is the start marker of the local colour and makes a world object (drawn at the cell centre, `FUN_0100cd00`) of each record whose id has the tile flag 0x10 or 0x20 (plants, droppers, effect-like ids). Every other record is ignored: it is read and never used.

**The remake did.** The renderer drew every ignored record as a sprite by the animation its dictionary entry names (id 296 even as a different rock than its own id), the grid made a flower stem an obstacle by the entry's name for any record, and a map without hill cells took every record of block 1, plants included, for a hill.

**Now.** The renderer draws only the records with the tile flag 0x10 or 0x20 (by id), the stem overlay is for those records only, and the fallback hills are the start markers (team below 4) only. The reader keeps the records (the sweep still counts them).

**Maps** (`LF4.4`): 736 records of the ids 0, 63 .. 66, 148, 151 and 296 in 20 maps (the dictionary names of 63 .. 66 are the power-ups'). Sweep: no state hash and no play of the 558 maps changes (the engine never used them); what changes is the picture of the maps whose ignored records name an animation.

## 5. Food is the tile flag 0x02

**The original.** A layer-2 cell is food when its id carries the tile flag 0x02 (`FUN_010071dd`; 87 ids), for the harvest rules, the fog (an unexplored anchor hides a pile), the minimap and the drop-tile tests. The dictionary's name is not asked.

**The remake did.** `Grid::init_from_level` and the renderer took an entry called `fd...` or `food...` for food, so a food id that the dictionary leaves "." or names otherwise was an obstacle overlay that nobody could harvest by sight, and `fdpez2b1` (id 344) and `fdfrl7` (id 287), which are not food ids, were piles. A stage tile of a food object marked every cell of its footprint as food, flag or not.

**Now.** `is_food` is the flag of the id: in the loader's layer-2 pass, in the footprint cells that `set_food_tile` writes, and in the renderer's static objects (stage drawing and fog). A food id is never an obstacle overlay.

**Maps** (`LF5.3`): 33 maps hold food ids that their dictionaries leave "." or unnamed; 2 (!TreasureIsland, Arena101) hold `fd...` entries of ids that are not food; 15 more have a block-2 stage tile that is not a food id (grass, rocks, power-ups). 50 maps change in all; none changes in play within 2,400 ticks (the sweep's players harvest the piles that exist by id), they differ in the state hash and in the cells' obstacle overlay (35 maps).
