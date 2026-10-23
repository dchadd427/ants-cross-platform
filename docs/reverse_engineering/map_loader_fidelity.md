# The map loader as the original's: what v0.12.10 changed

Where the remake's level loader (`Grid::init_from_level`, the level reader, the renderer's copy of the hill search) decided by a dictionary NAME, by a guess or by its own rule, it now does what the original does, read from the program (the tile-info tables, the loader `FUN_01006349` and its helpers, `SetTile` `FUN_01007352`). The original was read, never run. Each fix is its own commit with its own test (`tests/test_sim/test_loader_fidelity.cpp`, suite 2.9.4) and was swept over the 558 community maps (`tools/map_sweep`, 2,400 ticks, two rosters, three runs each: no crash, no hang, every run deterministic).

## All seven together

Against v0.12.9, over the 558 community maps of that day (24 of them have been taken out since, `Community-Maps/README.md`; the numbers below are those of the day, except where a test pins them) (the same sweep, `map_sweep` with its scripted players for 2,400 ticks, rosters of four teams and of green and black, three runs each; the loaded state compared with a scratch tool): the loaded walkability changes in 115 maps, the terrain in 22, the hills in 7, the start markers in 6, the food flags in 56 and the obstacle overlays in 348; the play changes in 116 maps, and the state hash of 440 more moves only because it now holds the solid bits that were cleared before (the six original maps play exactly as before). No crash, no hang, no run that is not deterministic.

## Numbers

Every fix below changes how a level is read, so by the rule next to `kProtocolVersion` the network protocol is 19 (nothing on the wire changed) and the replay rules numbers are 5 (the original's game) and 6 (game mode 187); recordings of rules 1 to 4 are an earlier version's ([REPLAYS.md](../REPLAYS.md#the-file)). One bump covers all the fixes.

## 1. Power-ups, food, fire walls and lunchboxes are solid when the file's solid bit says so

**The original.** A cell blocks by its layer-1 solid bit (`FUN_0100cf0f`), which the loader copies from the file (`FUN_010069d8`). A power-up, a food pile, a fire wall or a lunchbox is not solid by being one: `SetTile` (`FUN_01007352` -> `FUN_01007a22`) copies the tile flag 0x01 of the id to the bit when it places the object, and clearing the cell (`FUN_0100744f` -> `FUN_0100660c(row, col, 0)`) clears it again, row 0 never. A power-up that a file leaves on a cell without the bit is ordinary ground: paths cross it, ants stand on it, nothing happens (a power-up is taken only by the arrival of a move or power-up order whose tile it is). The exceptions of `CanEnter` and of the path step cost (the power-up order onto that tile, a harvest, a fire ant on fire) are evaluated only inside the `solid != 0` branch.

**The remake did.** `Grid::is_solid_object` counted every power-up, pile, lunchbox, fire wall and placeholder as solid by itself, and `init_from_level` cleared their file bits. `POPcOrN.lvl` (all 32 power-ups without the bit) therefore had its corridors plugged.

**Now.** The file's bit is kept and is the only source for a level read from a file. What the game places sets it and what it removes clears it, as `SetTile` does: `place_powerup`, `place_firewall`, `set_fire_at`, the flower dropper's power-up, `set_food_tile` (the footprint of every stage tile, so also the lunchbox), `set_layer2` (the placeholder 160, bridge stages, empty) and `place_bomb` (replacing the placeholder). A world made by hand (`init_empty`: tests and tools) has no file bits and keeps counting these objects as solid themselves. The cells under a pile's footprint also stop being power-ups (they kept the flag while holding the pile's tile).

**Maps.** Power-ups without the bit: 2,913 cells in 92 maps; food: 182 cells in 35; fire walls: 439 in 2 (`LF1.6`). Sweep of the 558 maps (loaded state, and 2,400 ticks with the sweep's scripted players, rosters 15 and 9): the walkability of the map at load changes in 107 maps; the play changes in 101 (the state hash of 451 others only because it now holds the bit that was cleared before; the six shipped maps play exactly as before, `LF1.7`); no crash, no hang, every run deterministic.

## 2. Hills and start markers are tile ids

**The original.** The hill of a color is the layer-2 id 245 + color (245 black, 246 blue, 247 red, 248 green; `FUN_0100ecac`, `FUN_0100ecdf`), the start marker of a color the block-1 id of the table at 0x1002468 (152 black, 153 blue, 154 green, 155 red; `FUN_0100ec18`). The dictionary is never asked (the four hill ids and the four markers are in the table of forced graphics). Block 1 starts an ant for a record only when its id is one of the four markers of the team's color.

**The remake did.** The level reader named a record's team by substrings of its dictionary entry (`GSTART`, `GREENHILL`, `ghill` ...), and `Grid::init_from_level`, `LevelData::for_roster`, the renderer and `map_sweep` found the hills by the names `greenhill` ... `blackhill`. A damaged entry lost a hill or a team's start ants, a permuted dictionary swapped the colors of hills.

**Now.** `ants::assets::hill_team_of_id` and `start_marker_team_of_id` (`lvl_parser.hpp`) are the one place that knows the ids; the reader, the grid (hill origins, the hill cells' overlay rule), `for_roster` (the erased hill of a color nobody plays), the renderer (hill art and anchor, what the static pass skips) and `map_sweep` use them. A block-1 record that is not one of the four markers has team 255 (a hill-named record in block 1 starts no ant any more).

**Maps** (`LF2.4`): 7 maps change by hills (CRISS CROSS, Conquer, LIFELESS, X-pRtfight lost a hill to a damaged entry; NO LIMIT, TREZ OF COMBAT, TREZ OF LIFE had colors swapped by a permuted one), 6 by start markers (BOMBERWORLD, CRISS CROSS, Conquer, NO LIMIT, TREZ OF COMBAT, TREZ OF LIFE): 8 maps in all, all of which play differently; none of the other 550 changes in any way (state hash and 2,400-tick play identical). CRISS CROSS and Conquer have four teams now (`players` 4 in `Community-Maps/maps.json`). Their pictures change (`test_loader_fidelity_render`): the hills of those 7 maps.

## 3. The terrain class of a layer-1 cell is the original's table by tile id

**The original.** `FUN_01008af7` takes the class of a cell (0 grass, 1 sand, 2 water, 3 mud, 4 dirt; a bridge on layer 2 makes it mud) from the table at 0x1001360 indexed by the layer-1 tile id (the remake carries the same 1,344 entries in `movement_tables_data.inc`). The dictionary's name of the entry and every flag bit of the cell but the solid bit (the loader keeps bit 0 of the cell word and drops the rest, `FUN_010069d8`) play no part. The ids 1 and 2 are grass.

**The remake did.** `Grid::init_from_level` applied the table only to a tile whose dictionary name was not ".". A lake, a mud path or a dirt road that the editor left unnamed was grass (ants walked across a lake), a cell with the flag 0x100 was water, the id 2 water and the id 1 a wall, all named ".".

**Now.** The class is the table's, by id, for every layer-1 cell (`determine_terrain_type` is gone).

**Maps** (`LF3.5`): 14 maps. Water 476 left unnamed: 960 cells in 1 map (Wata fight: ants crossed a lake of over 900 cells); mud 435 .. 444 in the Gant2 family (Gant2, gant 2, gauntlet2) and eze jungal; dirt 559 in the Turtle World family (!TurtleWorld, #Turtle World, Turtle World, Turtle); the flag 0x100 on unnamed tiles in Sand Box, SandBox and sand-box (1 each); and, found by the same rule, the id 1 (a wall in Bloom) and 2 (water in 's 4 Player Royal), one cell each. Sweep: the loaded terrain changes in these 16 maps and the play (2,400 ticks) in 11 of them (the other 5 only in the hash); `IsLaNd FiRe BoOm` and `hell2`, two of the 16, have since been taken out of the folder, which leaves 14; no other map changes; no crash, no hang, every run deterministic. The pictures of the 16 maps changed (the minimap and the map picture color the cells by class).

## 4. The block-1 records that the original ignores

**The original.** Block 1 is a list of (tile id, row, column). The match screen (`FUN_0100dbb0`) starts an ant for each record whose id is the start marker of the local color and makes a world object (drawn at the cell centre, `FUN_0100cd00`) of each record whose id has the tile flag 0x10 or 0x20 (plants, droppers, effect-like ids). Every other record is ignored: it is read and never used.

**The remake did.** The renderer drew every ignored record as a sprite by the animation its dictionary entry names (id 296 even as a different rock than its own id), the grid made a flower stem an obstacle by the entry's name for any record, and a map without hill cells took every record of block 1, plants included, for a hill.

**Now.** The renderer draws only the records with the tile flag 0x10 or 0x20 (by id), the stem overlay is for those records only, and the fallback hills are the start markers (team below 4) only. The reader keeps the records (the sweep still counts them).

**Maps** (`LF4.4`): 721 records of the ids 0, 63, 66, 148, 151 and 296 in 17 maps (the dictionary names of 63 .. 66 are the power-ups'; ids 64 and 65 were in maps that are gone). Sweep: no state hash and no play of the 558 maps changes (the engine never used them); what changes is the picture of the maps whose ignored records name an animation.

## 5. Food is the tile flag 0x02

**The original.** A layer-2 cell is food when its id carries the tile flag 0x02 (`FUN_010071dd`; 87 ids), for the harvest rules, the fog (an unexplored anchor hides a pile), the minimap and the drop-tile tests. The dictionary's name is not asked.

**The remake did.** `Grid::init_from_level` and the renderer took an entry called `fd...` or `food...` for food, so a food id that the dictionary leaves "." or names otherwise was an obstacle overlay that nobody could harvest by sight, and `fdpez2b1` (id 344) and `fdfrl7` (id 287), which are not food ids, were piles. A stage tile of a food object marked every cell of its footprint as food, flag or not.

**Now.** `is_food` is the flag of the id: in the loader's layer-2 pass, in the footprint cells that `set_food_tile` writes, and in the renderer's static objects (stage drawing and fog). A food id is never an obstacle overlay.

**Maps** (`LF5.3`): 29 maps hold food ids that their dictionaries leave "." or unnamed; 2 (!TreasureIsland, Arena101) hold `fd...` entries of ids that are not food; 11 more have a block-2 stage tile that is not a food id (grass, rocks, power-ups). 42 maps change in all; none changes in play within 2,400 ticks (the sweep's players harvest the piles that exist by id), they differ in the state hash and in the cells' obstacle overlay (35 maps in the sweep of that day, before 11 maps were taken out of the folder: of the 50 then, 8 are gone).

## 6. SetTile refuses a tile whose graphic is not loaded

**The original.** `SetTile` (`FUN_01007352`) on layer 2: when the cell's tile differs, the old object is cleared first (`FUN_0100744f`); then, for a tile other than 0x7ffe, the tile's template is looked up (`vtable + 0xc` of the animation set) and the call returns 0, without placing anything, when the id is past the table or its template is missing. A template is loaded for every id whose dictionary entry is not exactly "." (`FUN_0100674e`: the keep flag is `strcmp(name, ".") != 0`) and for the 62 ids of the forced table at 0x1001ba8 (`FUN_010068be`, checked byte for byte against the program); the rest are dropped by `FUN_0100707a`. Everything the game places with `SetTile` is a forced id (bridges, fire wall, placeholder, bombs, power-ups, lunchbox) except the stage tiles of food objects. The file's own layer-2 words are stored as they are, never through `SetTile`.

**The remake did.** It placed every stage tile whatever the dictionary called it.

**Now.** `Grid` knows which templates the level loads (`tile_graphic_loaded`; a world made by hand has all) and `set_food_tile` / `set_layer2` clear the old object and place nothing when the new tile's template is missing. A food object whose first stage is such a tile has no pile on the map (the object keeps its units, nobody can see or harvest it); one whose next stage is such a tile loses its pile at the bite that changes the stage. An entry that is empty (not ".") keeps its graphic, as `strcmp` says; an id past the dictionary is taken as not loaded (the original reads an uninitialised table there).

**Maps** (`LF6.5`): 34 maps have a block-2 stage tile whose entry is "." and not forced (the REA notes count 40 by also counting 5 maps whose entry is empty, which the program keeps). In the sweep of that day, when it was 35 maps (`WAR TIME` has since been taken out), the loaded map differed at the start in 16 of them (the file's tile at the anchor is cleared and the stage tile is not put there) and the 2,400-tick play in 11; the other 19 only lose a later stage. Not a verified observation: the original was read, never run.

## 7. No obstacle overlay from a "." entry

**The original.** The dictionary's name never changes behaviour; it only decides whether a graphic is loaded (fix 6). A layer-2 cell with such an id exists, blocks exactly as its solid bit says and keeps the flags and class of its id.

**The remake did.** Every layer-2 cell whose entry was "." or empty (and was not a power-up, bomb, hill or food) became an `is_obstacle_overlay` cell: impassable for the start-ant placement and the other tests of `TileCell::is_passable` and `is_obstacle`, whatever its solid bit said.

**Now.** The "." branch is gone. (A named object keeps the remake's name-based overlay for rocks, grass clusters and so on; that is not changed here.)

**Maps** (`LF7.3`): 99,904 layer-2 cells in 465 maps have an entry that is "." or empty (the folder today, pinned by the test; 101,806 in 484 maps on the day of the sweep of the 558 below); 339 maps changed in the overlay flag (and so in the state hash), 6 in play within 2,400 ticks (BattleGround!!, BattleGroundnn, COMBAT MIRAGE, FlowerPatch2, FlowerPatch3, FoReSt). Sweep of the 558: no crash, no hang, every run deterministic, `community_maps.py verify` clean.

## 8. A fire wall of a file is drawn from its cell, and leaves the screen when it is put out (v0.12.108, the picture only)

**The original.** Drawing reads what the cells hold (`FUN_01008089`, mode 2): a fire wall is the layer-2 id 134, one of the 62 forced graphics, so its dictionary entry never decides whether it is drawn (fix 6 above). A fire ant's put-out is `SetTile(2, 0x7ffe)`: the cell is empty and clears its solid bit, nothing is drawn there any more, and a fire that the file holds was never lit by an ant, so no lifetime task was armed for it ([`GAME_REVERSE_ENGINEERING.md`](../GAME_REVERSE_ENGINEERING.md) 5.3: the 180 seconds start when the fire is placed in play); the remake gives it no timer, and it burns until it is put out.

**The remake did.** The map maker names the entry `wallup04`, and the renderer made a static object of every layer-2 cell with a name, so a file's fire wall was drawn for ever, also after the Fire ant had put it out (the grid was right: the cell was empty, not solid, walkable and could be lit again; only the picture stayed). Power-ups and bombs were already left to the cells; a fire wall with the entry `.` too, which is why the library never showed it.

**Now.** The renderer's static pass skips id 134 like the power-up and bomb ids. While the fire burns the pixels are the same as before (the runtime pass drew it over the static one at the same place). The simulation is not changed.

**Maps** (`LF8.2`): 5 maps of the folder name the entry `wallup04` and hold 480 fire wall cells (ANTS_PARTY-V.3.0.5, A_Coaster_Mix_Turnament_Map, Welcome_from_chaos, thief craker2.0, ~~PlAyEr'S_rEvEnGe2); 214 other maps hold 20,158 with the entry `.`. **Left as it is, a decision for the rules:** the name-based obstacle overlay of fix 7 still marks the 480 named cells, and it stays on the cell after the fire is put out. In play it only refuses a dying ant's power-up drop onto that tile (every walk, ignite and plant test reads the solid bit); the bots' bomb placement also reads it. Taking the overlay away changes the state hash of those 5 maps from tick 0 (the ledger shows exactly those 5 lines, at ticks 0, 200 and 2,400), so it needs the rules numbers of "Numbers" above, not a quiet fix.
