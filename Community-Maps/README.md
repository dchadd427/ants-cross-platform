# Community maps

Maps that players of the 1998 game made and shared (`.lvl` files, the same format as the six maps in `Original-Ants/Maps`). The collection comes from the community's launcher server, which kept them for years; the files are byte for byte as they were collected. This folder holds the **502** of its 586 maps that the remake's engine draws and plays properly; nothing on a page or in a menu offers them yet.

**They are not part of the project's licence.** Each map belongs to the player who made it, and the makers were not recorded (some descriptions name them). If you made one and want it out, open an issue and it is taken out.

## What is in the folder

- `*.lvl`: the maps, under the names they were shared with (spaces and signs included; the names follow the protocol's rule for a map name).
- `maps.json`: one line for each map: `file`, `size`, `hash` (FNV-1a 64, the identity the network protocol gives a map file), `width`, `height`, `minutes`, `players` (the teams that have a hill or a start marker) and `description` (the 30 characters of the file's header that the setup screen shows as Map Info, as the maker wrote them). Made by `tools/community_maps.py`.

450 of the 502 files are different maps; the other 52 are the same map shared under a second name (`AlleyAnts` and `Alley Ants`), kept because the collection lists both.

## What was left out, and why

Of the collection's 586 files: 7 are the original game's maps (the six in `Original-Ants/Maps`, and `FOOD.lvl`, which is Tiny under another name: a file with the bytes of an original is never taken in), 77 were not taken in:

| Files | Why |
| ---: | --- |
| 39 | The engine does not load them. It follows the original's loader: it refuses a file that ends inside a block or is not a version 8 level, and a grid with no cell leaves no map to play. |
| 6 | A start marker lies outside the grid, so a team cannot be placed (the original does not check it and crashes or overwrites memory). |
| 22 | A tile is outside the file's own tile dictionary: the original draws whatever its memory holds there, so there is no picture to copy. |
| 10 | The file holds an email address (this repository is public). |

The 502 that remain load and play without a crash, a hang or an error, and give the same state hash in three plays. `tools/map_sweep.cpp` plays the roster of all four teams on each and, where the map has a start marker or a hill for both the first and the last team (green and black), those two teams alone; it played two minutes of game time each when they were taken in, and suite 2.18.1 of `./run_tests.sh` plays 30 seconds of each on every full run.

Odd things that the engine handles as the original does and that were kept: objects or waypoints outside the grid are ignored, a missing waypoint block means no waypoints, a mode word that is not 1 is ignored, and a missing egg stock gives every team no eggs (the original leaves it uninitialised).

## To use them

`ants_server --maps Community-Maps` lets rooms use any of them (`ants_server --help`); a public room takes only the maps of `--demo-maps`.

## To add maps

Put the new files and a copy of this folder's `.lvl` files in one folder, sweep it, and take in what passes (`build` writes `maps.json` again from that report):

```
cmake --build build --target map_sweep
./build/map_sweep SOURCE_DIR --out report.json
python3 tools/community_maps.py discards SOURCE_DIR report.json     # what would be left out, and why
python3 tools/community_maps.py build SOURCE_DIR report.json Community-Maps
```

A map is also left out when its name is one the network protocol refuses (none of the 586 is) or the sweep did not play it. The numbers in this README are pinned by a test (`tests/scripts/test_community_maps.py`, which holds the collection's 586 files and 7 originals itself, as they are not in the folder): update them together with the folder.
