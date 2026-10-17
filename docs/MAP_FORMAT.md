# The map file (.lvl)

The format of the game's map files as the remake reads and writes them, the limits a reader and the writer hold a file to, the original-safe profile (does a map work in the original game?), the round-trip law of the writer and how it is proven, and the dictionary and the blank map of a new map. The code: `include/ants_assets/lvl_parser.hpp` (the reader, `LevelLimits`, the findings of `LevelValidation`), `include/ants_assets/lvl_writer.hpp` (the writer and the rest) and `include/ants_assets/lvl_checker.hpp` (the original-safe profile); the tool: `lvl_tool` (below). What the original's loader does with a file, address by address, is [`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) sections 4.4 and 4.5; why the remake decides by tile id and not by the dictionary's names, [`reverse_engineering/map_loader_fidelity.md`](reverse_engineering/map_loader_fidelity.md).

## The layout

Every number is little-endian; a word is 2 bytes, a dword 4. The parts follow each other with no gap, in this order.

| Part | Bytes | `LevelData` | What it holds |
|---|---|---|---|
| Version | dword at 0 | `version` | 8. The original refuses any other. (Version 9, the Extended map of this remake: [below](#version-9-the-extended-map).) |
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

## Version 9: the Extended map

The map has two kinds. A **Classic map** is a version 8 file, the original's format, with every limit above: it plays in the original game and in this remake. An **Extended map** is a version 9 file: the same parts in the same order with a wider header and no 256-cell side, for this remake only. Version 8 is read, checked and written exactly as it always was (the same bytes, limits, findings and state hashes); version 9 is a sibling that a reader takes only when its caller says so (`LevelLimits::allow_extended`, below). In this version of the game nothing reads or writes version 9 except the tests, `lvl_tool` and the conversion functions: no caller of the game, the server or the browser module enables it.

| Part | Version 8 | Version 9 |
|---|---|---|
| Version | dword at 0: 8 | dword at 0: 9 |
| Mode, minutes | dword at 4, word at 8 | the same |
| Format flags | none | dword at 10 (`LevelData::format_flags`): the bits of features the format may add later. **0 today.** A reader refuses a file with a bit it does not know (`format_flags_unknown`, fatal), so a later version of the format cannot be misread by an older reader. |
| Rules | none | word at 14 (`LevelData::rules`): **0 = the original's rules exactly, 1 = extended rules.** Any other value is refused (`rules_out_of_range`, fatal). The simulation does not read the word yet; a converted Classic map keeps 0, so it plays as it did, and a new Extended map (`blank_extended_level`) asks for 1 when it has more than 90 rows (the original's path finder never enters tile row 90) or more than 256 columns. |
| Description | 30 bytes at 10 | a word at 16 (the length, at most 255), then that many bytes; read up to the first zero byte as in version 8 (`description_too_long` if the length is above the limit) |
| Dictionary | word at 40, then 11 bytes per name | the same encoding, after the description; the same cap of 1,344 names |
| Grid size | two dwords, the lower 16 bits kept | two dwords, **every bit honored**, then checked against the limits and the ceiling below before a byte of the cells is counted or allocated |
| Layer 1, layer 2 | 6 bytes per cell | 6 bytes per cell. **The third word of a layer-2 cell that holds an object is the anchor relative to the cell**: a signed byte for the row offset in the low byte and a signed byte for the column offset in the high byte, so no position needs a wide number. In a map larger than 256 on a side an object is at most 127 cells (-128 to 127) from its anchor; in a map of 256 x 256 or less the sum wraps modulo 256 (below), so every anchor byte can be stored. The third word of the original's bomb tiles (129 to 132, the owner of the bomb) and of the empty tile 0x7FFE stays as it is. |
| Blocks 1 to 4, eggs | words | the same, 65,535 entries a block |
| After the egg word | never read | never read |

`LevelData` holds a version 9 level as the file does: `MapCell::properties` is the file's word, so the relative form of the anchor. `LevelData::object_anchor(row, column, anchor_row, anchor_column)` gives the absolute anchor of either version and `has_object_anchor` says which cells have one; the simulation will use them when it reads version 9. **In a map of at most 256 rows and 256 columns the sum of cell and offset is taken modulo 256**, so the anchor byte of a Classic map keeps its value through a conversion even when it points outside the grid (13 cells of 8 library maps hold such a byte, for example 0xCDCD), and the conversion is lossless for every map. In a larger map the sum is plain and may be negative or above 255, which `convert_to_classic` reports. **So the anchor encoding depends on the size of the map: the same bytes mean different anchors when a map grows past 256 on a side.** Example: in a 200-wide map, a cell in column 10 with its anchor in column 190 stores the offset +180, which is the signed byte -76; read in a 300-wide map the same byte puts the anchor in column -66. An editor must therefore read every anchor through `LevelData::object_anchor` before a resize across 256 (rows or columns) and write it again (relative to its cell, within -128 to 127 for the larger map) after it; an object that is further than 127 cells from its anchor in the larger map cannot be kept as it is.

A version 9 reader is **strict where the version 8 reader copies the original's loader**: a food pile without stages, a file that ends inside block 4 or before the egg word are not read (`food_block_ended_early`, `truncated`, fatal), because the quirks of that loader are not a format to keep. Bytes after the egg word are never read, as in version 8.

**Limits.** Both kinds, and the ceiling that no `LevelLimits` can exceed once Extended maps are allowed:

| | Classic (version 8) | Extended (version 9): `LevelLimits::for_extended_maps()` | Ceiling of the format (`LevelLimits::format_ceiling()`) |
|---|---|---|---|
| A side | 256 by the format; a new map 130 (`for_new_maps()`) | 1,024 | 2,048 |
| Cells | 65,536 by the format; a new map 10,000 | 262,144 in all, any shape (512 x 512, 1,024 x 256, ...) | 4,194,304 |
| A file as it is read | no limit; a new map 512 KB | 4 MiB | 64 MiB (refused before a byte is read) |
| The canonical file that is written | no limit; a new map 144 KB | 3.5 MiB (a 512 x 512 map is 3.1 MiB) | 64 MiB |
| Dictionary | 1,344 names | 1,344 | 1,344 |
| Entries in a block | 65,535 | 65,535 | 65,535 |
| Description | 30 bytes in the file (29 characters that the original can end) | 255 bytes | 255 bytes |
| Teams | four | four | four (the format flags leave room) |

No compression in this version of the format: 12 bytes a cell. The reader's memory rule holds for version 9: at most 7 times the file and 64 KiB at once, and no allocation for a count before the bytes it announces exist (a header that announces 65,535 x 65,535 or 2^32-1 cells is refused as `grid_too_large` and allocates nothing; the cell count is computed in 64 bits).

**A place that reads Classic maps only refuses version 9.** `LevelLimits::allow_extended` is `false` for `LevelLimits{}` and `for_new_maps()` and `true` for `for_extended_maps()` and `format_ceiling()`. The game, the server and the browser module pass the first two, so given a version 9 file they answer, without reading it, `version_not_supported` (fatal: "header version 9 is an Extended map: version 9 is not supported here, this place reads Classic maps (version 8) only"), not a crash and not a misreading; `write_level` refuses a version 9 level the same way, and `LevelData::validate` says it of a level in memory. A place enables Extended maps in its own change, with its own limits.

**What the original does with a version 9 file.** It reads version 8 only (the loader report, 0x1006386): it shows "The MAP you tried to play is the wrong version" and the match starts without a map. Not a crash and not a misreading. (Not tried in the original game itself.)

**The original-safe profile of an Extended map** is not a failure: `check_original_safe` answers a version 9 level, or a file whose version word is 9 (without reading it), with the one finding `not_for_original` ("not for the original game (needs a version 8 file)"). Its severity is note (it is neither fatal nor a warning), `works_in_original_game()` is false, `not_for_original()` is true and `judged` and `fits_the_place` stay true. The profile for the remake and the wording of the checks by kind are another change.

**Conversion** (`include/ants_assets/lvl_writer.hpp`; `lvl_tool convert FILE --to extended|classic -o OUT`). A conversion always makes a copy: another level, written as another file with another hash; the source is kept.

- `convert_to_extended(level)`: always possible for a Classic map. Every field is carried, the anchors become relative, `rules` 0 and `format_flags` 0, the description is kept. A level that is Extended already is copied.
- `convert_to_classic(level)`: only when the map fits: both sides at most 256, the dictionary at most 1,344 names, the description at most 30 bytes (the file's field; a description of exactly 30 bytes converts with a note, because the original reads no zero byte that ends it and the checker warns), no format flags, every object anchor inside a byte (0 to 255). Rules 0 or 1: rules 1 is dropped with a note (the original's rules apply to a Classic map: ants cannot cross tile row 90, a long path order can fail). When it does not fit, `reasons` lists every cause in a sentence with the number and the limit ("this map is 400 columns wide; a Classic map has at most 256 columns", "the description is 31 bytes; a Classic map holds 30"). The policy of new Classic maps (90 rows) is a limit of the places that make maps, not of the conversion.
- `needs_extended(level)`: the same reasons, empty when the level can be a Classic map.

**The laws** (all in `tests/test_assets/test_lvl_writer.cpp` W7 and `tests/test_sim/test_map_roundtrip.cpp`): (1) a Classic map goes up and comes back as the same bytes as its canonical file, for all 540 maps of the library; (2) read, write, read of a version 9 file is the same level and writing twice gives the same bytes; (3) a converted map (rules 0) plays with the same state hashes as its source. The simulation does not load version 9 in this version of the game, so the play check gives it the converted level with its anchors turned absolute again by `object_anchor`, which is all a version 9 loader will do; (4) version 8 is unchanged (the bot baselines, the round-trip counts of the table above, the recorded replays); (5) hostile input: the headers above, unknown flag bits, rules out of range, a cut at every byte, bytes changed at random with fixed seeds, a version 9 file given to a Classic-only reader, a version 8 file given to the version 9 reader, the memory bound; and the mutants of `tests/data/lvl_extended_mutants.json`.

## The original-safe profile

`check_original_safe(level)`, or of a file's bytes (`lvl_tool check FILE --original`), answers "does this map work in the original game?". **The yardstick: a map works in the original game when the original loads it without refusing it or reading outside its tables, and plays what the map shows** (a hill and starting ants for each of the four teams, every food pile where the map puts it). It is stricter than "the remake plays it": every file that the remake refuses or plays with a warning has a fatal finding or a warning here too. Each finding has a stable code (the editor and the server key on it), a severity, one sentence in plain words ("Blue has no hill", "one food pile stands outside the map (row 12, column 40)") and the cell it is about when there is one; a report lists the findings that stop the map first, in the order of the rules below, then the notes.

- **Fatal**: the original refuses the file, reads outside its tables or walks memory that the file did not fill.
- **Warning**: the original loads the map and plays it otherwise than the map shows (a missing hill, a pile that nobody can see).
- **Note**: something the disassembly notes say the original tolerates, which has not been tried in the original game itself; a note that a map is found to fail on there becomes a warning.

**"Works in the original game" is true when there is no fatal finding and no warning**; notes do not count. The rules come from the disassembly ([`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) 4.4, 4.5 and 5.62, [the loader report](reverse_engineering/map_loader_fidelity.md); the addresses are those of `Ants.exe`).

| Code | Severity | The rule, and what the original does with a map that breaks it |
|---|---|---|
| `file_unreadable`, `file_too_large` | fatal | Of a file: some bytes, and no more than the place that reads it allows (`LevelLimits`). A file that is too large is not read at all: nothing is known of the map, and the report says it was not judged. |
| `file_cut_short` | fatal | The file holds every part up to block 3. One that ends sooner stops the program with "System read file error." (the loader's one `try` holds only block 4 and the egg word). |
| `file_cut_in_block_4` | fatal | Block 4 (the flower droppers) is whole. Cut there, the original keeps the records it read, but its table has a slot for every record the file announced: the dropper poll walks the slots that were never filled and the Map's destructor frees them (4.5), and the egg stock of every team is whatever its heap held. |
| `egg_word_missing` | warning | The egg word (the number of eggs each team starts with) is there. Cut off, the egg stock of every team is whatever its heap held (the egg field is never set otherwise). |
| `food_block_ended_early` | warning | Block 2 ends where its count says. A pile without stages ends it on the spot (0x1006dc9), and blocks 3, 4 and the egg word are read from the bytes behind it. |
| `version_not_8` | fatal | Version 8: any other is "wrong version", and the match starts without a map (0x1006386). |
| `not_for_original` | note, and no "works in the original game" | An [Extended map](#version-9-the-extended-map) (version 9): the original cannot load it. Not a fault of the map: the one finding of such a map. |
| `description_too_long` | warning | A description of at most 29 bytes (not letters: a browser editor writes UTF-8, and a letter outside a to z takes 2 or 3 bytes): the original reads 30 bytes without a check (0x10063c3), and 30 leave no zero byte to end the text. |
| `grid_empty`, `grid_too_large`, `layers_not_grid` | fatal | 1 to 256 rows and columns, the remake's own limits (the original loads 0 rows into a map without a cell; an object's anchor holds its row and its column in a byte each), and, for a level in memory, layers of rows x columns cells. |
| `dictionary_too_large` | fatal | At most 1,344 names: more overrun the original's buffer of 0x1500 bytes (0x1006795). |
| `tile_outside_dictionary` | fatal | Every tile of the layers, of block 1, of the food stages and of block 3 inside the dictionary: the remap table is read with the raw index (0x10067a5, 0x1007057), and 0x100665a indexes an array on the stack with what it read. The empty tile 0x7FFE of a layer-2 cell, a stage and block 3 is no tile. |
| `ground_missing` | fatal | Every ground cell has a tile: the terrain class is a table of the 1,344 ids (FUN_01008af7), and the empty tile 0x7FFE on layer 1 is read past its end. |
| `start_marker_outside_map`, `food_pile_outside_map` | fatal | Every start marker and food pile inside the grid: the original writes through their row and column without a check (FUN_0100ef18, FUN_01007352). |
| `food_pile_without_stages` | fatal | Every food pile has a stage: a pile without stages ends block 2 there (above). |
| `hill_missing`, `hill_without_entrance`, `hills_several` | warning | Each of the four teams has one hill: the layer-2 cells of its tile (245 black, 246 blue, 247 red, 248 green), one of them marked as the entrance (flag bit 0: the cell is the hill's origin, FUN_010076b6), from which the original makes the team's home (FUN_0100ecdf). |
| `starting_ants_missing` | warning | Each of the four teams has a start marker (block-1 tile 152 black, 153 blue, 154 green, 155 red; FUN_0100ec18): one starting ant each. |
| `food_pile_not_shown` | warning | Every food pile is placed. A pile whose stage tile the dictionary names `.` (and that is not one of the 62 always-loaded ids) is not placed by SetTile (FUN_01007352, [the loader report](reverse_engineering/map_loader_fidelity.md) section 6): nobody can see or harvest it, and a later stage of that kind makes the pile vanish. The original plays the map otherwise than it shows (the remake does the same). |
| `mode_not_1` | note | Mode 1, what the original's maps hold. Any value loads: the original stores the mode and never reads it. |
| `hill_not_whole` | note | A hill is the 4 x 4 cells around its entrance, each anchored at it. The original takes each cell's anchor for its hill's origin, so a cell anchored elsewhere, or a cell of the hill's tile elsewhere, is part of another "hill" there. When every cell that is missing lies off the map, the sentence says so and does not name a cell as the one that differs. |
| `tile_not_drawn` | note | A tile that the dictionary names `.` is not loaded (FUN_0100674e) unless it is one of the 62 ids that are always loaded (FUN_010068be, `LVL_ALWAYS_LOADED_TILES`): the original does not draw it. |
| `object_outside_map` | note | Plants and waypoints outside the grid: the original ignores them. |

**Limits of the place are not rules of the original.** When the checker is given the limits of a new map (`lvl_tool check FILE --original --new-map`), a grid, a dictionary or a file beyond them (131 columns, 10,100 cells, a file over 512 KB) is reported with the flag `place_limit`: the sentence says "allowed here", the map is judged by the original's rules all the same (a 131-column map that has its hills and ants works in the original game), `fits_the_place()` is false, and `lvl_tool` says it on a line of its own ("too big for a new map (a limit of the map maker, the original has none)"), never under "does not work in the original game". Its exit status is 1 for either. A file too large to be read is not judged at all (`judged` is false).

The profile's choices: the mode is a note, because the original never reads it; `ground_missing`, `hill_without_entrance` and the notes from `hill_not_whole` on were found in the same notes, besides the rules that every map needs. A new map that takes the dictionary rule (below) and places whole hills never trips a note. The notes have not been tried in the original game itself. **Rules not made**, because the notes are silent on what the original does or the original's own maps do it: a hill whose queue tiles fall outside the grid, a food pile whose stage tile reaches past the edge, a match of 0 minutes, and **hills of two teams that overlap** (a red 4 x 4 over cells of green's: only green's hill is then "not whole", a note; the home tiles of one team may land on the other's hill, which the notes do not describe). Tests: `test_original_safe` (suite 1.7): for every rule a level that breaks it and one that keeps it at its boundary, the files that the reader refuses, 3,000 files with bytes changed at random (fixed seeds), each of which works in the original only when the remake plays it without a warning, and the library.

**The library** (`test_original_safe` S5): **485 of the 540 maps work in the original game**, the six original maps among them, with no finding at all. The 55 others (the first reason decides the row; a map with two reasons is in the row of the first):

| Why | Maps |
|---|---|
| the file ends inside block 4 (`file_cut_in_block_4`, fatal) | 1v1TREASURE, AntsTournament, Ants_Town, ArCaDeStYLe, Arena101, BetterArcadeStyle, Bunker, Conquer (and a description of 30 bytes), Food Island, Food Rules The Ants!!!, NW |
| the file ends before its egg word (`egg_word_missing`) | AnTsCaStLe |
| a description of 30 bytes (`description_too_long`) | !Small Winter, ANTS_PARTY-V.3.0.5, Bridge, Deadly Limbs, TheGreatSea (and a pile that does not show), Water of 2000, no_name_map |
| a food pile outside the map (`food_pile_outside_map`, fatal) | KiM Trez, TREZ OF COMBAT (and a pile that does not show), TREZ OF LIFE (and a pile that does not show) |
| two hills without an entrance (`hill_without_entrance`) | IcoN, Mud Wars |
| a food pile that does not show (`food_pile_not_shown`) | 31 maps, with 3 more above: !TurtleWorld, #Turtle World, AGGRIVATION, BATTLE DOME, Beginners, Bombs, BombzAway_Delux, BombzAway_Supreme, COMBAT MIRAGE, Changing Medium, ConflictPlus, CrimeWar, DEATH2001, FlowerPatch2, FoReSt, Gant2, HELL, MAZE II, Mega Treasure, Perzia, Sights_Unseen, SuMoNe's MaP, TRAP, Tabatha, Turtle World, Turtle, aNtS_oF_1998, crapwhole, gant 2, gauntlet2, who mode heaven down |

(34 maps have a pile that does not show: 31 only that, 3 in the rows above.) 11 of the 12 files that are cut short work once they are rewritten (the rewrite holds the waypoints read, whole, and the egg word that the remake plays, 0); Conquer's description still stops it; the 11 stay "does not work" until their files are rewritten. Notes: tiles named `.` that the map uses on 93 maps, hills that are not whole on 36, plants or waypoints outside the grid on 36, a mode other than 1 on 14.

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
| Go up to an Extended map (`convert_to_extended`), written, read again as the same level, written twice as the same bytes, refused by a Classic-only reader | 540 |
| Come back (`convert_to_classic`) as the same bytes as the rewrite | 540 |
| The Extended map (rules 0) plays the same as its source (the simulation is given its anchors absolute again; [Version 9](#version-9-the-extended-map)) | 540 |
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

## In the browser

The map core runs in a page: `lvl_core.js` + `lvl_core.wasm` are the reader, the checker, the writer, the blank map and `same_level` compiled from the same C++ files as the game and `lvl_tool` (`src/ants_assets/lvl_parser.cpp`, `lvl_writer.cpp`, `lvl_checker.cpp`, and `lvl_core.cpp`, which turns their results into plain data), so the page, the server and the tools cannot disagree about a map. It is a module of its own (no SDL, no game code, no file system), built by the web build and shipped in the web image beside `index.wasm`; no page loads it yet (the map maker's page is a later pull request), and nothing about the game or the site changes. **It still answers Classic maps only**: it passes the limits of a Classic map, so its reader refuses a version 9 file with `version_not_supported` and its checker answers `not_for_original` ([Version 9](#version-9-the-extended-map)); every answer for a version 8 file is the same as before.

**The interface** (`include/ants_assets/lvl_core.hpp` in C++; `src/ants_assets/lvl_core_c.cpp` for the module). Bytes go in, and JSON text or bytes come out. Every call is a pure function of its arguments: nothing is kept between calls, nothing is thrown (a failure is a status), and the limits are `LevelLimits::for_new_maps()` (at most 10,000 cells and 130 a side, 512 KB read, 144 KB written) unless the call passes `profile` 1, the game's own limits.

| Call | Answers |
|---|---|
| `lvl_read_report(bytes, profile)` | JSON: `bytes`, `loaded`, `reason` (why it does not load), `version`, `mode`, `minutes`, `description` and `description_bytes`, `rows`, `columns`, `dictionary_names`, `tile_ids_used`, `eggs`, `default_type_tile`, `food_piles`, `waypoints`, `teams` (start markers and hill cells of each of the four), `playable` for all four teams, and `findings` (`severity` fatal, warning or note, `code`, `message`, `team`, `count`). A file that does not load has `loaded` false and the findings that say why. (The file holds no name: the description is the only text.) |
| `lvl_check_original(bytes, profile)` | JSON: `judged`, `works_in_original_game`, `fits_the_place` and the findings of [the original-safe profile](#the-original-safe-profile), each with `severity`, `code`, `message`, `row`, `column`, `team`, `count` and `place_limit`, in the checker's order and words (the lines `lvl_tool check FILE --original --new-map` prints). |
| `lvl_rewrite(bytes, profile)` | the canonical bytes of the file; a refusal (status 1) with `lvl_tool`'s sentence for a file that does not load, does not play with all four teams or cannot be written within the limits. |
| `lvl_blank(rows, columns, ground_tile, eggs, minutes, profile)` | the bytes of a new map of plain ground (tile 485 `g01a` is the ground of the original maps; any id below 1,344 is taken), no hill and no start marker yet; its dictionary is the one of [a new map](#the-dictionary-of-a-new-map), from the names in `src/ants_assets/lvl_tile_names.inc` (the names of ants.chd's Table 4, which the module has because a page has no ants.chd; `test_lvl_core` checks the list against the archive). |
| `lvl_same_level(a, b, profile)` | JSON `{"same":true}` or `{"same":false,"difference":"..."}`; status 1 when either file does not load. |

Each call returns a **result block** (`0` when there is no memory left for it): a dword status, a dword size, then the payload; `lvl_result_status`, `lvl_result_size` and `lvl_result_data` read it. Status 0 is the answer, 1 a refusal and 2 a fault or no memory, both with a sentence as the payload. **The caller frees every block with `lvl_free`, and every input buffer it got from `lvl_alloc` the same way** (a memory that may grow to 256 MB; a call that runs out of memory answers status 2, with no sentence at all when even that did not fit, which the wrapper words as "out of memory"). `web/lvl_core_api.js` does all of this (copy in, call, copy out, free) and answers `{status, text, json, bytes, error}`:

```js
createLvlCore().then(function (module) {
    var core = LvlCore.wrap(module);                 // web/lvl_core_api.js, a plain script that a page and node can load
    var report = core.readReport(bytes);             // bytes: a Uint8Array; report.json.findings, report.json.teams, ...
    var canonical = core.rewrite(bytes).bytes;       // the canonical file
    var blank = core.blank(31, 31, 485, 6, 10).bytes; // rows, columns, ground tile, eggs, minutes
});
```

**Text and browsers.** The format is DOS-era, so a description or a dictionary name is bytes, not UTF-8: the JSON gives every byte of 0x7F or more as the code point `\u00XX` (Latin-1), so the text is valid whatever the file held and a byte is never lost. A refusal sentence can hold text of the file, so a page shows every sentence and every description with `textContent`, never `innerHTML`. The module is built with WebAssembly exception handling (`-fwasm-exceptions`): Chrome 95, Firefox 100 and Safari 15.2 or newer; an older browser rejects the module when it is instantiated, and the page must catch that and show a plain sentence. `LvlCore.wrap` throws when the module is not the interface version it needs (an old `lvl_core.js` cached beside a new `lvl_core_api.js`), and after the module has thrown (a wasm trap) `isDead()` is true and every later call answers status 2 until the module is created again. The raw accessors read a null block as status 2 with no payload.

**To build it:** it is part of the web build (`./build_web.sh`, `docker build .`, `tools/web_without_docker.py`), or alone: `emcmake cmake -B build_web -DBUILD_TESTS=OFF && cmake --build build_web --target lvl_core`, which writes `build_web/lvl_core/lvl_core.{js,wasm}`. The desktop and server builds do not have the target; the only change to them is that the library `ants_assets` has one more small source (`lvl_core.cpp`). **Size** (Emscripten 3.1.58, `-Oz -flto`): `lvl_core.wasm` 104,843 bytes (41,126 gzipped, which nginx serves), `lvl_core.js` 12,451 bytes (3,932 gzipped), `lvl_core_api.js` about 6 KB.

**How it is tested.** The native test `test_lvl_core` (`tests/test_assets/`, suite 1.8 of `./run_tests.sh`, ctest) runs the C interface over nine real maps of the library (a 31 x 31, a 40 x 40 and a 60 x 60 original; a 60 x 60 that shows no food pile; a 31 x 31 and a cut file whose description has 30 bytes; the largest file, 235,594 bytes; the largest grid, 81 x 100; a hill without an entrance), over hostile files and over blank maps, and keeps the answers in `tests/data/lvl_core/` (`test_lvl_core --write-expected` writes them again after a change that is meant): the reports, the findings, the size and FNV-1a hash of each rewrite, the output of `lvl_tool check FILE --original --new-map`. The Node test `tests/scripts/lvl_core_check.js` loads the **module** and holds it to that expectation, byte for byte (the same core, used everywhere); then it checks that the findings are the lines `lvl_tool` prints (live with `--lvl-tool`), that the canonical rewrite of each file reads back as the same level and rewrites to itself, that a blank map of each size fits the place and has only the missing hills and starting ants (`hill_missing`, `starting_ants_missing`), and that hostile input (no bytes, one byte, cut files, 513 KB, a header that announces 65,536 names or 4 billion rows, random bytes, arguments that are no bytes or numbers) gets an error report: no throw, no hang. A fresh module then makes the same calls 300 rounds over (about 8,000 calls): its heap does not grow and a 1 MB buffer is given the same place before and after, so every buffer is given back (a no-op `lvl_free` fails it). The wrapper's version check and its dead core after a trap are tested with stand-in modules, and `test_lvl_core` makes every allocation fail in turn in every call (case C6: always a status, never an exception out of a call). It does not compare a match fingerprint (no native tool prints one for a map file); the rewrite plays the same because it is the same level, which is the writer's [round-trip law](#the-round-trip-law). Run it with `node tests/scripts/lvl_core_check.js` (`--module DIR` for another folder than `build_web/lvl_core`, `--lvl-tool build/lvl_tool` for the live comparison); `tests/scripts/test_lvl_core.py` runs it in the python tier when a module is built and says it was not run when not. CI runs it in the web job on the module it takes out of the image it just built (`--require`).

## lvl_tool

```text
cmake --build build --target lvl_tool
./build/lvl_tool check map.lvl [--roster MASK] [--new-map]          # loads? plays for these teams? every finding
./build/lvl_tool check map.lvl --original [--new-map]                # works in the original game? every finding with its code; with --new-map, too big for a new map? (on a line of its own)
./build/lvl_tool info map.lvl                                        # (an Extended map: also its kind, flags, rules, and whether it fits a Classic map) grid, dictionary, start markers and hills of each team, food, waypoints, eggs, works in the original game?, what a rewrite changes
./build/lvl_tool rewrite in.lvl out.lvl [--roster MASK] [--new-map]  # checks it and writes its canonical bytes; says what differs
./build/lvl_tool blank out.lvl --rows 31 --columns 31 [--new-map]    # a blank map of 1 to 256 rows and columns (the tile names from Original-Ants/ants.chd, or --chd FILE)
./build/lvl_tool blank out.lvl --extended --rows 300 --columns 400   # a blank Extended map (version 9): at most 1,024 a side and 262,144 cells
./build/lvl_tool convert in.lvl --to extended|classic -o out.lvl     # a copy as the other kind (to classic: only when it fits, else the reasons are listed); in.lvl is kept
./build/lvl_tool --selftest
```

(`build\lvl_tool.exe` on Windows.) `--roster` is a mask of the teams that play (1 green, 2 red, 4 blue, 8 black; 15, all four, by default); `--new-map` holds the file to the limits of a new map. A file is read up to 64 MiB, or 512 KB with `--new-map`: one that holds more is not read to its end, and it does not load. `convert` writes a copy (another file than FILE) and nothing when the map does not fit a Classic map (it lists why); `check` and `rewrite` of a version 9 file say `version_not_supported`, `check --original` says `not_for_original`, and `info` describes it. `rewrite` writes nothing for a file that does not load or does not play for the roster, and it writes through a new file beside OUT, which takes OUT's place once it is whole (so OUT may be IN). The exit status is 0 when the file plays (`check`; with `--original`, when it works in the original game and, with `--new-map`, is no bigger than a new map may be), is described (`info`) or is written (`rewrite`, `blank`), 1 when it does not load, does not play (does not work in the original game) or cannot be written within the limits, and 2 for a wrong command line, a file that cannot be read or written, or no memory left.
