# Community maps

Maps that players of the 1998 game made and shared (`.lvl` files, the same format as the six maps in `Original-Ants/Maps`). The collection comes from the community's launcher server, which kept them for years. This folder holds the **547** of its 586 maps that the remake's engine draws and plays properly: 502 byte for byte as they were collected, and 45 that were damaged and are repaired (below). A map that cannot be played by a team that it claims is not kept ("Taken out again", below). Nothing on a page or in a menu offers them yet.

**They are not part of the project's licence.** Each map belongs to the player who made it, and the makers were not recorded (some descriptions name them). If you made one and want it out, open an issue and it is taken out.

## What is in the folder

- `*.lvl`: the maps, under the names they were shared with (spaces and signs included; the names follow the protocol's rule for a map name).
- `maps.json`: one line for each map: `file`, `size`, `hash` (FNV-1a 64, the identity the network protocol gives a map file), `width`, `height`, `minutes`, `players` (the teams that have a hill or a start marker) and `description` (the 30 characters of the file's header that the setup screen shows as Map Info, as the maker wrote them); a repaired map has one more field, `repaired`, a sentence that says what was changed. Made by `tools/community_maps.py`.

- `previews/` and `previews.json`: a picture of every map (below).

492 of the 547 files are different maps; the other 55 are the same map shared under a second name (`AlleyAnts` and `Alley Ants`), kept because the collection lists both.

## Bombs in the files

206 of the maps hold bombs of their own: layer-2 cells with the original's bomb ids 129 (red), 130 (green), 131 (black) and 132 (blue), 61,609 cells in all (`Bombz Away` fills its floor with 896 of them, `Bombzzzzz away!!!` with 3,536). The maps' own dictionaries mostly call these entries `.` (the editor left them unnamed), which is why an earlier remake took them for rocks and never drew them. The remake now plays them as the original does: each is a bomb of its colour with the owner team that the file names, shown on the field (not on the minimap, as in the original), and the first ant that steps on one sets it off. How and why: [the report](../docs/reverse_engineering/map_file_bombs.md). The map files are untouched.

## The 45 repaired maps

A repaired file is new bytes under the same name; the bytes of the maps that were not repaired are untouched. A repair (`tools/repair_maps.py`, tested in `tests/scripts/test_repair_maps.py`) is a function of the file's bytes alone and changes only what is damaged; each repaired file is swept like the others and is in the folder only because it passes. Three kinds of damage were found:

| Files | Damage | Repair |
| ---: | --- | --- |
| 21 | The file went through a text transfer: a `0x0D` byte lost the `0x00` behind it (the word 13), a `0x0D 0x0A` pair stands for a `0x0A` (the word 10) or a `0x0D` was lost in front of a `0x0A`. Every record behind the first change is shifted, so the engine reads garbage or runs into the end of the file. | The inverse is searched in the whole file along the file format. It is taken only when the cheapest reading ends exactly at the end of the file, has at most two cells of a kind that no map has, and the next best reading has more. All 21 meet that. The maps differ from the file only by the bytes put back or taken out. |
| 4 | A start marker lies outside the grid (the row is `0x5408`, or 36 on a grid of 31). | When the low bytes of both coordinates are inside the grid the marker goes there (the same marker of the sister maps has row 8); else the marker is dropped. Three maps got a marker moved, one a marker dropped (its team keeps four). |
| 20 | A tile is outside the file's own tile dictionary: the original draws whatever its memory holds there, so there is no picture to copy. For 11 maps it is block 3's word (4094 where the other maps have `0x7FFE`, "no default ant type"), for 10 maps 1 to 10 cells (`eze jungal` has both). | Block 3's word becomes `0x7FFE` (the engine takes an index outside the dictionary as "no default" already, so the play does not change). A layer 1 cell takes the tile that most of its eight neighbours have, its flags and properties kept (the engine reads them as they are); a layer 2 cell becomes the empty cell. |

`21`: `!!MoDE!! ViCToRY`, `!Suicide!`, `AFT_RANK MAP`, `ALL IN ONE !`, `A_Coaster_Mix_Turnament_Map`, `Acid Blood`, `Cherry_Cream`, `Christmas_Lakes`, `DANGERWORLD!`, `DEATH WAR!!!!!!!!!!!!ARENA 3`, `EM_WAR_OR_DEAD`, `Fight Food`, `GRiM-FAiT`, `HiTLer`, `Jedi Owns`, `Obstacles`, `PoPcOrN-ReVoLuTiOn`, `Popcorn 3.0`, `ShowDown`, `thief craker2.0`, `~~PlAyEr'S_rEvEnGe2`.

`4`: `Chicken Battles`, `ChickenBattles`, `TheGreatSea`, `TheGrid`.

`20`: `CRISS CROSS`, `Easy jungle`, `Easyjungle`, `Easyjungle2`, `FlowerPatch2`, `ForestFire`, `Grid`, `JuNgLe2`, `JuNgLe5`, `Jungle`, `Jungle2000`, `Jungle3`, `KiM Trez`, `Monkeys on Islands`, `Mud Wars`, `Odd`, `PoP KoRn Trez`, `PoWeR`, `Super jungle`, `eze jungal`.

The exact change of each file is its `repaired` sentence in `maps.json`.

## What was left out, and why

Of the collection's 586 files: 7 are the original game's maps (the six in `Original-Ants/Maps`, and `FOOD.lvl`, which is Tiny under another name: a file with the bytes of an original is never taken in), 21 were not taken in, as they are or repaired:

| Files | Why |
| ---: | --- |
| 4 | The file is a program (it starts with `MZ`), not a map: `Lake`, `Pacant`, `Mountains`, `PRo Clan`. |
| 3 | The header is not a level's: `MIDNIGHT664` (bytes that are no header), `Xpert sept 4 green` (it starts in the middle of a map's cells), `PiGs MiGhT Fly` (version 8, then only zeros: no grid, no tiles). |
| 3 | The file is cut off: `DEATH WAR AREA 2` ends after the first byte of block 1's count, `EM_red_copy_machine` inside block 2's count, `RIVER` inside the tile dictionary. What followed is lost, not damaged, and the engine, like the original, refuses such a file. |
| 9 | Bytes are inserted, removed or overwritten in a way that is no text-transfer damage (a zero byte became `0x20`, `0x43`, `0xFF` or another value from some place on), so the records cannot be told apart: `!!GotBalls`, `Drop Out`, `EM_MINE_STAFF`, `EM_Messy_Place_NO_Power_ups`, `FlowerPatch`, `Gundam Highways`, `PopCorn VS Bomzz Away!!`, `Popup bombs`, `Screen`. |
| 2 | Almost all tiles of a layer are outside the dictionary (929 of 961 cells, 1491 of 1600): the layer is garbage and replacing the tiles would make another map: `Obstacles2` (its block 2 ends early too), `The Matrix`. |

The 547 that are in the folder load and play without a crash, a hang or an error, and give the same state hash in three plays. `tools/map_sweep.cpp` plays the roster of all four teams on each and, where the map has a start marker or a hill for both the first and the last team (green and black), those two teams alone; it played two minutes of game time each when they were taken in, and suite 2.18.1 of `./run_tests.sh` plays 30 seconds of each on every full run.

Odd things that the engine handles as the original does and that were kept: objects or waypoints outside the grid are ignored, a missing waypoint block means no waypoints, a mode word that is not 1 is ignored, and a missing egg stock gives every team no eggs (the original leaves it uninitialised).

## Taken out again

11 of the 558 maps that were first taken in were removed (the owner's rule: a map that is broken does not belong in the repository). Each loaded and was swept without a crash, but no match could be played on it by every team that it claims (`players` in `maps.json` is the number of teams with a start marker or a hill, or 4 when it has neither). The files are in the git history.

- **A team has a hill but no start marker, so it has no ant** (10 maps; eggs alone cannot be hatched without points, and no food is brought in): `Home Bound`, `IsLaNd FiRe BoOm` and `Thief craker` have no start marker for any team (an empty world), `K-P`, `Pulse2` and `Pulse3` none for the blue and black teams, `QuItErS`, `firewar` and `hellzone` none for red, `WAR TIME` none for blue.
- **Every ant drowns** (1 map): `hell2` is the lake of `HELL`: all 31 x 31 cells are water but a block of 16 rocks, and the 15 ants of each team start in it (the same file as `HELL` with the land under the start squares and the match time changed); they all drown on tick 48 and the match is over on tick 49 in every roster, even when ordered out on tick 1.

`test_map_list` case ML1.4 (suite 2.19.9) fails when a map in the folder is Fatal for a roster that plays has a start marker for fewer teams than its `players` claims, or kills all the ants of a team that plays alone (or ends that match) within a minute; `tools/community_maps.py` leaves such a map out of a new folder with the reason "no start marker for a team".

## The previews

`previews/<file name without .lvl>.webp` is the map as the game's own setup screen draws it in its map preview: `tools/map_previews.py` runs the real game headless on that screen with only that map in its Maps folder (the method of the six `web/front/preview_*.png`), cuts the preview out (300 x 300 pixels; a map that is not square has black bars, as on the screen) and saves it as a lossy WebP at quality 85 (about 16 KB each, about 9 MB in all; smaller pictures or a 16-colour PNG wiped out the small sprites). The setup screen's preview is the map, not a match: no ants, no fog, no HUD. `previews.json` is `{"<map file>": {"hash", "picture", "encoding"}}`: the hash is the one of `maps.json`, the encoding names the size and quality; a picture is drawn again only when its map's hash or the encoding is not the listed one or the picture is missing. Nothing else reads them yet; `maps.json` is not changed.

```
cmake --build build --target ants
python3 tools/map_previews.py --jobs 4          # Community-Maps/ into Community-Maps/previews/; --ants PROGRAM, --out DIR, --force, another FOLDER
```

One map takes about 3 seconds; all 547 with 4 jobs about 7 minutes. A run that stops goes on where it was.

## To use them

`ants_server --maps Community-Maps` lets rooms use any of them (`ants_server --help`); a public room takes only the maps of `--demo-maps`.

## To add maps, or to make this folder again

The steps are the same: sweep the source files, repair what does not pass, sweep the repaired files, and take in what passes. `SOURCE_DIR` holds the original files of the collection (this repository does not keep them); `repaired` and the `.json` reports are made on the way and can be thrown away.

```
cmake --build build --target map_sweep
./build/map_sweep SOURCE_DIR --out source.json
python3 tools/community_maps.py repair SOURCE_DIR source.json repaired                         # new bytes for what has a repair, and repaired/repairs.json
./build/map_sweep repaired --out repaired.json                                                 # the repaired files are swept on their own
python3 tools/community_maps.py discards SOURCE_DIR source.json repaired repaired.json       # what stays out, and why
python3 tools/community_maps.py build SOURCE_DIR source.json Community-Maps repaired repaired.json
./build/map_sweep Community-Maps --out final.json                                              # the folder itself, as the CI sweeps it
python3 tools/community_maps.py verify Community-Maps final.json                              # prints nothing when all is well
```

A map is also left out when its name is one the network protocol refuses (none of the 586 is), the sweep did not play it, or a team that the map claims has no start marker (the next section). The numbers in this README are pinned by tests (`tests/scripts/test_community_maps.py`, which holds the collection's 586 files and 7 originals itself, as they are not in the folder): update them together with the folder.
