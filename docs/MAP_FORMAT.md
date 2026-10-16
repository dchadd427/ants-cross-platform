# The map file (.lvl)

The format of the game's map files as the remake reads and writes them, the limits a reader and the writer hold a file to, the original-safe profile (does a map work in the original game?), the round-trip law of the writer and how it is proven, and the dictionary and the blank map of a new map. The code: `include/ants_assets/lvl_parser.hpp` (the reader, `LevelLimits`, the findings of `LevelValidation`), `include/ants_assets/lvl_writer.hpp` (the writer and the rest) and `include/ants_assets/lvl_checker.hpp` (the original-safe profile); the tool: `lvl_tool` (below). What the original's loader does with a file, address by address, is [`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) sections 4.4 and 4.5; why the remake decides by tile id and not by the dictionary's names, [`reverse_engineering/map_loader_fidelity.md`](reverse_engineering/map_loader_fidelity.md).

## The layout

Every number is little-endian; a word is 2 bytes, a dword 4. The parts follow each other with no gap, in this order.

| Part | Bytes | `LevelData` | What it holds |
|---|---|---|---|
| Version | dword at 0 | `version` | 8. The original refuses any other. |
| Mode | dword at 4 | `game_mode` | 1 in the original maps; the original stores it and never reads it. |
| Minutes | word at 8 | `default_minutes` | The length of a match. |
| Description | 30 bytes at 10 | `description` | A text up to its first zero byte. |
| Dictionary | word at 40, then 11 bytes per name | `tile_type_count`, `tile_dictionary` | The word is the number of names less one. Name i belongs to tile id i and is read up to its first zero byte. |
| Grid size | two dwords | `height`, `width` | The **first** dword is the number of rows, the second the number of columns; the original keeps the lower 16 bits of each. |
| Layer 1 | 6 bytes per cell | `layer1_terrain` | Per cell three words: the tile id, a flags word (bit 0: the cell is solid) and a third word. The rows are the outer loop: cell (row r, column c) is entry r x columns + c. |
| Layer 2 | 6 bytes per cell | `layer2_interactive` | The same three words; tile 0x7FFE (32,766) is an empty cell, and the third word is the anchor of the object the cell belongs to (its row in the low byte, its column in the high byte). |
| Block 1 | a word, then 6 bytes per record | `anthill_spawns` | (tile id, row, column): the start markers (152 black, 153 blue, 154 green, 155 red) and the plants; the original ignores every other record. |
| Block 2 | a word, then per food pile 10 bytes and 4 per stage | `food_schedules` | Per pile: row, column, units (`initial_delay`), points per unit (`respawn_interval`), the number of stages; per stage the threshold of units left (`weight`) and the tile shown (`tile_id`). (The field names are those of an older reading of the block.) |
| Block 3 | two words | `ambient_flag`, `ambient_tile_or_sound` | A word the original drops, then the level's default ant type as a tile id (62 combat, 63 thief, 64 bomber, 65 swimmer, 66 fire; anything else is none). |
| Block 4 | a word, then per waypoint 8 or 52 bytes | `waypoints` | Row and column (words) and a flag (dword); when the flag is not 0, an interval (dword, seconds) and five chances (doubles): the flower droppers. |
| Eggs | the last word | `boundary_param` | The egg stock of every team. |

**A tile index of the file is the game's tile id**: the original's dictionary remap is the identity. What the ids mean (hills 245 black, 246 blue, 247 red, 248 green on layer 2; power-ups 62 to 66; the bombs 129 to 132; food by the tile flag of its id; the terrain class by a table of ids) is [the loader report](reverse_engineering/map_loader_fidelity.md). The dictionary's names are not ids: a name of `.` tells the original not to load that tile's graphic (62 ids are always loaded), so such a tile is not drawn and the game does not place it ([`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) 5.62); the remake's renderer and its obstacle overlay for named objects read the names too. That is why a copy of a map keeps its dictionary.

## The canonical file

`write_level(level, limits)` writes every field as the level holds it, in the order above, with zeros after the end of the description and of each name, and nothing after the egg word; a waypoint without trigger (flag 0) takes 8 bytes. It refuses, with one sentence that says why, a level that no reader can have made: another version, a grid without a cell or beyond the limits, layers that are not rows x columns, texts that do not fit their fields or hold a zero byte, a `tile_type_count` that is not the number of names less one, more than 65,535 entries in a block, a start marker whose team is not that of its tile, a food pile without stages, a waypoint without trigger that holds an interval or chances. It also refuses a level whose canonical file is larger than the limit of a written file (`max_write_bytes`, below) or than the limit of a file that is read (`max_read_bytes`: what it writes reads back with the same limits), and a level that a reader took can be one: the limits of a new map read a file of up to 512 KB and write one of 144 KB, and a file cut short grows when its blocks are written whole, by up to 6 bytes (the rest of a half waypoint and the egg word: `Conquer.lvl`, 19,128 bytes, is rewritten as 19,134). Before it answers it reads its own bytes back and compares the level (`same_level`), so what it returns always reads back as the same map. A canonical file is its own rewrite.

**A rewrite is another file.** The network game knows a map by the hash of its bytes (`net::hash_file`, FNV-1a 64: the start barrier compares it, and a replay records it), so a rewrite, whose bytes differ from the file's, is another map there. A map of the library is never replaced by its rewrite in place: a rewrite is a new file.

## Limits

| | The game: `LevelLimits{}` | A new map: `LevelLimits::for_new_maps()` |
|---|---|---|
| Grid | 256 a side (the anchor of a layer-2 object is a byte for each) | 10,000 cells, 130 a side |
| Dictionary | 1,344 names (more overrun the original's tables) | 1,344 names |
| A file as it is read (`max_read_bytes`) | no limit | 512 KB (524,288 bytes) |
| The canonical file that is written (`max_write_bytes`) | no limit | 144 KB (147,456 bytes) |

The reader refuses a file beyond its limits (`FileTooLarge` before a byte is read, `GridTooLarge`, `DictionaryTooLarge`) and the writer a level beyond them; a limit can only be made tighter than the game's. A new map is one that the map maker saves or that a server takes from a player, and any map of the library can be the start of one, so the limits of a new map are those of the library. The size is limited twice: a file is read as it is up to 512 KB, so that every file of the library is read (the largest, `BOMBERWORLD.lvl`, has 235,594 bytes, 176,741 of them after its egg word, which its rewrite of 58,853 bytes drops), and the canonical file that is written has at most 144 KB. **The arithmetic:** a file has 50 bytes of header, count word and grid size (42 + 8), 11 bytes a name, 12 bytes a cell (6 in each layer) and its blocks, which are 12 bytes when they are empty (the three counts, block 3 and the egg word) and then 6 bytes a block-1 record, 10 a food pile and 4 a stage, 8 a waypoint (52 with a trigger). A 100 x 100 map with the largest dictionary, 1,344 names, is therefore 50 + 14,784 + 120,000 + 12 = 134,846 bytes, and 144 KB leave it 12,610 bytes of blocks (242 waypoints with a trigger and one food pile of four stages fill them to the byte); a blank 100 x 100 map (486 names) is 125,408 bytes. The 130 a side are for the widest map of the library, Bomb Races (40 rows of 129 columns, 5,160 cells); the most cells of a library map are 8,100 (81 x 100). Every map of the library is within the limits of a new map, as a file and rewritten (the largest rewrite is 113,880 bytes, Easy jungle).

On hostile input the reader never asks for memory for a count before it has the bytes the count announces, and it gives back the room of a count that the file does not fill (a block 2 that ends at a food pile without stages) before it reads on. What it holds at once is at most 7 times the size of the file and 64 KiB: a waypoint without trigger, 8 bytes of file, is 56 bytes in memory, and nothing of a file costs more (a dictionary name about 3 times, a cell once). The writer holds at most 8 times the file it writes and 64 KiB: the bytes, and the level it reads back from them. From a file (`load_from_file`, `check_file`) the reader also holds the file's bytes: 8 times the file and 64 KiB. With the limits of a new map that is at most 3.6 MiB for a file that is read from memory (64 KiB + 7 x 512 KiB), 4.1 MiB for one read from a file (64 KiB + 8 x 512 KiB) and 1.2 MiB for one that is written (64 KiB + 8 x 144 KiB); the writer refuses a level beyond the limits before it takes anything of its size, and `blank_level` makes no cell of a grid beyond them. Tests: suite 6c of `test_assets` (the reader, the limits) and `test_lvl_writer` W4 and W6.4 (files cut at every byte, bytes changed at random with fixed seeds, every count of a file set to five values from 0 to 65,535 (0, 1, 32,767, 32,768 and 65,535), the dearest files (waypoints without trigger, a half record after them, a block 2 that announces 65,535 food piles and ends at once, 512 KB of waypoints), grids of 65,535 x 65,535, and a heap that counts what they take).

## The original-safe profile

`check_original_safe(level)`, or of a file's bytes (`lvl_tool check FILE --original`), answers "does this map work in the original game?": the original loads it without refusing it or reading outside its tables, and finds in it what the map shows, a hill and starting ants for each of the four teams. It is stricter than "the remake plays it": every file that the remake refuses or plays with a warning has a fatal finding or a warning here too. Each finding has a stable code (the editor and the server key on it), a severity, one sentence in plain words ("Blue has no hill", "one food pile stands outside the map (row 12, column 40)") and the cell it is about when there is one; a report lists the findings that stop the map first, in the order of the rules below, then the notes. **"Works in the original game" is true when there is no fatal finding and no warning**; notes do not count. The rules come from the disassembly ([`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) 4.4, 4.5 and 5.62, [the loader report](reverse_engineering/map_loader_fidelity.md); the addresses are those of `Ants.exe`).

| Code | Severity | The rule, and what the original does with a map that breaks it |
|---|---|---|
| `file_unreadable`, `file_too_large` | fatal | Of a file: some bytes, and no more than the place that reads it allows (`LevelLimits`). |
| `file_cut_short` | fatal | The file holds every part up to block 3. One that ends sooner stops the program with "System read file error." (the loader's one `try` holds only block 4 and the egg word). |
| `file_cut_in_block_4`, `egg_word_missing` | warning | Block 4 and the egg word are there. Cut there, the original keeps what it read, and the egg stock of every team is whatever its heap held (the egg field is never set otherwise). |
| `food_block_ended_early` | warning | Block 2 ends where its count says. A pile without stages ends it on the spot (0x1006dc9), and blocks 3, 4 and the egg word are read from the bytes behind it. |
| `version_not_8` | fatal | Version 8: any other is "wrong version", and the match starts without a map (0x1006386). |
| `description_too_long` | warning | A description of at most 29 characters: the original reads 30 bytes without a check (0x10063c3), and 30 leave no zero byte to end the text. |
| `grid_empty`, `grid_too_large`, `layers_not_grid` | fatal | 1 to 256 rows and columns (an object's anchor holds its row and its column in a byte each), and, for a level in memory, layers of rows x columns cells. |
| `dictionary_too_large` | fatal | At most 1,344 names: more overrun the original's buffer of 0x1500 bytes (0x1006795). |
| `tile_outside_dictionary` | fatal | Every tile of the layers, of block 1, of the food stages and of block 3 inside the dictionary: the remap table is read with the raw index (0x10067a5, 0x1007057), and 0x100665a indexes an array on the stack with what it read. The empty tile 0x7FFE of a layer-2 cell, a stage and block 3 is no tile. |
| `ground_missing` | warning | Every ground cell has a tile: the terrain class is a table of the 1,344 ids (FUN_01008af7), and the empty tile 0x7FFE on layer 1 is read past its end. |
| `start_marker_outside_map`, `food_pile_outside_map` | fatal | Every start marker and food pile inside the grid: the original writes through their row and column without a check (FUN_0100ef18, FUN_01007352). |
| `food_pile_without_stages` | fatal | Every food pile has a stage: a pile without stages ends block 2 there (above). |
| `hill_missing`, `hill_without_entrance`, `hills_several` | warning | Each of the four teams has one hill: the layer-2 cells of its tile (245 black, 246 blue, 247 red, 248 green), one of them marked as the entrance (flag bit 0: the cell is the hill's origin, FUN_010076b6), from which the original makes the team's home (FUN_0100ecdf). |
| `starting_ants_missing` | warning | Each of the four teams has a start marker (block-1 tile 152 black, 153 blue, 154 green, 155 red; FUN_0100ec18): one starting ant each. |
| `mode_not_1` | note | Mode 1, what the original's maps hold. Any value loads: the original stores the mode and never reads it. |
| `hill_not_whole` | note | A hill is the 4 x 4 cells around its entrance, each anchored at it. The original takes each cell's anchor for its hill's origin, so a cell anchored elsewhere, or a cell of the hill's tile elsewhere, is part of another "hill" there. |
| `tile_not_drawn` | note | A tile that the dictionary names `.` is not loaded (FUN_0100674e) unless it is one of the 62 ids that are always loaded (FUN_010068be, `LVL_ALWAYS_LOADED_TILES`): the original does not draw it. |
| `food_pile_not_shown` | note | A food pile whose stage tile is such a tile: SetTile (FUN_01007352) places nothing, so there is no pile from that stage on (the remake does the same). |
| `object_outside_map` | note | Plants and waypoints outside the grid: the original ignores them. |

The profile's choices: the mode is a note, because the original never reads it; the rules from `ground_missing` to `hill_without_entrance` and the notes from `hill_not_whole` on were found in the same notes (a map maker's own maps never break them: its dictionary names every tile it uses, and its hills are whole). Tests: `test_original_safe` (suite 1.7): for every rule a level that breaks it and one that keeps it at its boundary, the files that the reader refuses, 3,000 files with bytes changed at random (fixed seeds), each of which works in the original only when the remake plays it without a warning, and the library.

**The library** (`test_original_safe` S5): **516 of the 540 maps work in the original game**, the six original maps among them, with no finding at all. The 24 others:

| Why | Maps |
|---|---|
| the file ends inside block 4 (`file_cut_in_block_4`) | 1v1TREASURE, AntsTournament, Ants_Town, ArCaDeStYLe, Arena101, BetterArcadeStyle, Bunker, Conquer, Food Island, Food Rules The Ants!!!, NW |
| the file ends before its egg word (`egg_word_missing`) | AnTsCaStLe |
| a description of 30 characters (`description_too_long`) | !Small Winter, ANTS_PARTY-V.3.0.5, Bridge, Conquer, Deadly Limbs, TheGreatSea, Water of 2000, no_name_map |
| a food pile outside the map (`food_pile_outside_map`) | KiM Trez, TREZ OF COMBAT, TREZ OF LIFE |
| two hills without an entrance (`hill_without_entrance`) | IcoN, Mud Wars |

The 12 files that are cut short work once they are rewritten (the rewrite holds the waypoints read and the egg word that the remake plays, 0) unless something else stops them (Conquer's description). Notes: tiles named `.` that the map uses on 93 maps, hills that are not whole on 36, plants or waypoints outside the grid on 36, food piles that do not show on 34, a mode other than 1 on 14.

## The round-trip law

**Read a file, write the level, read the bytes again: the two levels are the same map, and the file and its rewrite play the same.** The rewrite differs from the file only where the original never reads a byte, or where the file was cut short:

- leftovers after the end of a dictionary name or of the description: the rewrite writes zeros there;
- the upper 16 bits of the two grid dwords: the rewrite writes the 16-bit numbers;
- bytes after the egg word: dropped;
- a block 2 that ends at a food pile without stages, a block 4 or an egg word cut short: the rewrite holds what was read, whole (the waypoints read, and an egg word of what the remake plays: 0). Such a rewrite plays in the remake as the file did; in the original it is the defined version of a file that left something to chance.

`describe_rewrite` accounts for every byte that differs by these kinds; any other difference is a fault of the writer. A value that the file holds is kept, whatever it is: the egg word 32,766 of the community editor's maps (32,766 eggs a team, in both games), a mode the original never reads, a waypoint's chances to the bit.

**The proof** (`tests/test_sim/test_map_roundtrip.cpp`, suite 2.9.6 of `./run_tests.sh`, run by CI's `ctest` on Linux, macOS and both Windows jobs) takes every map of the library, the six original maps and the 534 of `Community-Maps/maps.json`:

| Every map of the library | 540 |
|---|---|
| Read back as the same map (`same_level`: every field of the file) | 540 |
| Play the same (the full state hash at the start and after every game second; all four teams, the map sweep's scripted players, 200 ticks) | 540 |
| The same bytes as the file | 0: every file has leftovers after the end of its names (every name of TINY is `.`, its zero and then `\0\0.\level`: what was in the editor's buffer) |
| The same bytes as the file where the original reads it | 528 |
| Leftovers after the description | 14 |
| Bytes after the egg word | 64 (the community editor's template ends its maps with 206 bytes of filler) |
| Cut short inside block 4 or before the egg word | 12 |
| Rewritten again: the same bytes | 540 |

A play of 200 ticks (10 game seconds) keeps the whole proof at 10 to 15 seconds on a CI machine. The play is the second check on top of the field-by-field comparison: it would see a field that the comparison missed. The test shows that it can: TINY with one field changed (a cell's solid bit, a cell's tile, a power-up, a pile's units or points, a start marker, the eggs, the minutes, the default ant type) plays differently within those ticks, every time. To run it:

```text
cmake --build build --target test_map_roundtrip
./build/tests/test_sim/test_map_roundtrip                         # the 540 maps; the counts above are checked
./build/tests/test_sim/test_map_roundtrip --ticks 2400 --only TREASURE
```

## The dictionary of a new map

A copy of a map keeps its source's dictionary. A new map gets the dictionary that each of the six original maps has: **an entry for every tile id from 0 to the highest id the map uses, the name of every id it uses, and `.` for every other id**, the names being those of the animations of Table 4 of `ants.chd` (`tile_names_from_chd`; every named entry of the six originals is exactly that name). `dictionary_of_used_tiles` makes it, and makes the dictionaries of all six originals byte for byte (670 to 1,335 entries, 86 to 135 of them named). The players' maps came from other editors: 24 of the 534 follow the rule. Why this rule and not a fixed list: the dictionary of an original map (TREASURE's, the largest, names 135 of its 1,335 tiles) leaves `.` on the tiles a new map would use, which the original would not draw; a dictionary that names every tile would make the original load all 1,344 graphics, which none of its own maps does. The map maker applies the rule when it saves a new map.

## The blank map

`blank_level(rows, columns, names, limits)` is the starting point of a new map: every layer-1 cell is tile 485 (`g01a`, plain ground, the commonest layer-1 tile of all six original maps), not solid, with the third word 0xCDCD, which every layer-1 cell of the six originals holds (and every cell of 477 of the 534 players' maps; in 37 more it is the third word of most cells); layer 2 is empty; no start marker, hill, food or waypoint; no default ant type; version 8, mode 1, 10 minutes and 6 eggs a team (GAUNTLET's and MEDIUM's); an empty description; its dictionary by the rule above (486 entries, one name). A grid beyond `limits` gets no cell, so nothing of its size is allocated, and the writer refuses it by its grid.

## lvl_tool

```text
cmake --build build --target lvl_tool
./build/lvl_tool check map.lvl [--roster MASK] [--new-map]          # loads? plays for these teams? every finding
./build/lvl_tool check map.lvl --original [--new-map]                # works in the original game? every finding with its code
./build/lvl_tool info map.lvl                                        # grid, dictionary, start markers and hills of each team, food, waypoints, eggs, works in the original game?, what a rewrite changes
./build/lvl_tool rewrite in.lvl out.lvl [--roster MASK] [--new-map]  # checks it and writes its canonical bytes; says what differs
./build/lvl_tool blank out.lvl --rows 31 --columns 31 [--new-map]    # a blank map of 1 to 256 rows and columns (the tile names from Original-Ants/ants.chd, or --chd FILE)
./build/lvl_tool --selftest
```

(`build\lvl_tool.exe` on Windows.) `--roster` is a mask of the teams that play (1 green, 2 red, 4 blue, 8 black; 15, all four, by default); `--new-map` holds the file to the limits of a new map. A file is read up to 64 MiB, or 512 KB with `--new-map`: one that holds more is not read to its end, and it does not load. `rewrite` writes nothing for a file that does not load or does not play for the roster, and it writes through a new file beside OUT, which takes OUT's place once it is whole (so OUT may be IN). The exit status is 0 when the file plays (`check`; with `--original`, when it works in the original game), is described (`info`) or is written (`rewrite`, `blank`), 1 when it does not load, does not play (does not work in the original game) or cannot be written within the limits, and 2 for a wrong command line, a file that cannot be read or written, or no memory left.
