# The map file (.lvl)

The format of the game's map files as the remake reads and writes them, the limits a reader and the writer hold a file to, the rules a map must follow to load in the original game, the round-trip law of the writer and how it is proven, and the dictionary and the blank map of a new map. The code: `include/ants_assets/lvl_parser.hpp` (the reader, `LevelLimits`, the findings of `LevelValidation`) and `include/ants_assets/lvl_writer.hpp` (the writer and the rest); the tool: `lvl_tool` (below). What the original's loader does with a file, address by address, is [`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) sections 4.4 and 4.5; why the remake decides by tile id and not by the dictionary's names, [`reverse_engineering/map_loader_fidelity.md`](reverse_engineering/map_loader_fidelity.md).

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

`write_level(level)` writes every field as the level holds it, in the order above, with zeros after the end of the description and of each name, and nothing after the egg word; a waypoint without trigger (flag 0) takes 8 bytes. It refuses, with one sentence that says why, a level that no reader can have made: another version, a grid without a cell or beyond the limits, layers that are not rows x columns, texts that do not fit their fields or hold a zero byte, a `tile_type_count` that is not the number of names less one, more than 65,535 entries in a block, a start marker whose team is not that of its tile, a food pile without stages, a waypoint without trigger that holds an interval or chances. Before it answers it reads its own bytes back and compares the level (`same_level`), so what it returns always reads back as the same map. A canonical file is its own rewrite.

## Limits

| | The game: `LevelLimits{}` | A new map: `LevelLimits::for_new_maps()` |
|---|---|---|
| Rows, columns | 256 each (the anchor of a layer-2 object is a byte for each) | 100 each |
| Dictionary | 1,344 names (more overrun the original's tables) | 1,344 names |
| File | no limit | 120 KB (122,880 bytes) |

The reader refuses a file beyond its limits (`FileTooLarge` before a byte is read, `GridTooLarge`, `DictionaryTooLarge`) and the writer a level beyond them; a limit can only be made tighter than the game's. A new map is one that the map maker saves or that a server takes from a player. Every map of the library is within the game's limits; their rewrites are all at most 120 KB (the largest is 113,880 bytes, Easy jungle), and one is wider than 100: Bomb Races, 40 rows of 129 columns. The two limits of a new map do not hold together for the largest grids: 100 x 100 cells are 120,000 bytes of layers alone, so a blank 100 x 100 map is 125,408 bytes and the largest square blank map within 120 KB is 98 x 98; with a dictionary of 1,335 names (as large as an original map's) a 100 x 100 map has at least 134,747 bytes.

On hostile input the reader never asks for memory for a count before it has the bytes the count announces: what it holds at once is at most 64 KiB and 8 times the size of the file (a waypoint without trigger, 8 bytes of file, is 56 in memory), and the writer at most 64 KiB and 10 times the file it writes. Tests: suite 6c of `test_assets` (the reader, the limits) and `test_lvl_writer` W4 (files cut at every byte, bytes changed at random with fixed seeds, every count set to 0 .. 65,535, a heap that counts what they take).

## The rules of a map that loads in the original

A map that follows these loads in the original game and plays there as it says. They come from the disassembly ([`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) 4.4, 4.5 and 5.62, [the loader report](reverse_engineering/map_loader_fidelity.md)); the checker that applies them is not built yet.

1. Version 8: any other is refused with a message, and the match has no map.
2. Mode 1: any value loads, as the original never reads it; 1 is what its own maps hold.
3. A description of at most 29 bytes: 30 leave no zero to end the text.
4. Every tile id of the layers, of block 1, of the food stages and of block 3 inside the dictionary (an id beyond it reads past the original's table: a tile of whatever lies there), and at most 1,344 names.
5. Every start marker and food pile inside the grid: the original writes through their row and column without a check.
6. Every food pile has at least one stage: a pile without stages ends block 2, and the original reads blocks 3, 4 and the egg word from the bytes that follow it.
7. Blocks 3 and 4 and the egg word are there: a file that ends before block 4 stops the original with "System read file error."; one that ends inside block 4 or before the egg word leaves the egg stock to chance.
8. Each of the four teams has a hill (layer-2 tiles 245 to 248) and a start marker (block-1 tiles 152 to 155), so that it has a home and ants.

Found in the same notes and for the checker to weigh: a tile that the map uses and whose name is `.` is not drawn by the original (unless it is one of the 62 ids whose graphics are always loaded), and a food pile whose stage tile is such a tile shows no pile.

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

`blank_level(rows, columns, names)` is the starting point of a new map: every layer-1 cell is tile 485 (`g01a`, plain ground, the commonest layer-1 tile of all six original maps), not solid, with the third word 0xCDCD that every layer-1 cell of the six originals (and of 514 of the 534 players' maps) holds; layer 2 is empty; no start marker, hill, food or waypoint; no default ant type; version 8, mode 1, 10 minutes and 6 eggs a team (GAUNTLET's and MEDIUM's); an empty description; its dictionary by the rule above (486 entries, one name).

## lvl_tool

```text
cmake --build build --target lvl_tool
./build/lvl_tool check map.lvl [--roster MASK] [--new-map]          # loads? plays for these teams? every finding
./build/lvl_tool info map.lvl                                        # grid, dictionary, start markers and hills of each team, food, waypoints, eggs, what a rewrite changes
./build/lvl_tool rewrite in.lvl out.lvl [--roster MASK] [--new-map]  # checks it and writes its canonical bytes; says what differs
./build/lvl_tool blank out.lvl --rows 31 --columns 31 [--new-map]    # a blank map (the tile names from Original-Ants/ants.chd, or --chd FILE)
./build/lvl_tool --selftest
```

(`build\lvl_tool.exe` on Windows.) `--roster` is a mask of the teams that play (1 green, 2 red, 4 blue, 8 black; 15, all four, by default); `--new-map` holds the file to the limits of a new map. `rewrite` writes nothing for a file that does not load or does not play for the roster. The exit status is 0 when the file plays (`check`), is described (`info`) or is written (`rewrite`, `blank`), 1 when it does not load, does not play or cannot be written within the limits, and 2 for a wrong command line or a file that cannot be read or written.
