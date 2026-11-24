# World art

The new pictures of what lies on the map: the ground, the edges between two kinds of ground, the bridge the Swimmer digs. Later batches add the fire, the bomb, the explosion, the grass, the plants and the four hills. They are this project's own drawings, made by [`tools/world_art/`](../tools/world_art/README.md) (the drawings, the rebuild command, the limits of the pictures), and the original's sprites stay: the same setting that chooses the [HUD art](VIEW_AND_HUD.md#hud-art) chooses these, New (the default) or Classic, which draws the original's sprites exactly as before. Look only: nothing here reaches the simulation, the rules, a replay or a state hash.

## What it does

- **A picture replaces a sprite by its number.** Wherever the game would draw sprite N of the original (in a tile's animation, in a bridge's) it draws the pack's picture of N when there is one, at the place the animation gives and in the same size, so every animation, its frame times and its footprint are the original's. A picture that is not the original sprite's size times the scale is not used: that sprite is drawn as the original has it (`Renderer::draw_world_art_part`).
- **Which sheet.** Like the HUD art, the pack has one PNG sheet for each of the scales 1x, 2x and 3x. The picture of the world at the zoom 1 is drawn straight into the output, from the sheet that the output's scale needs (at most 1 pixel for each canvas pixel: 1x, up to 2: 2x, more: 3x: [`world_art.hpp`](../include/ants_app/world_art.hpp), the same rule as the HUD's). A zoomed pass (the mouse wheel) draws the world at its own pixels and enlarges it afterwards, so it uses the 1x sheet. A sheet that cannot be read, or that the device cannot make a texture of, gives the next smaller one; none gives the original's sprites (said once on stderr).
- **The setting.** **Art** in the [settings menu](GAME_MENU.md) (the key is still `hud_art`, and `--hud-art`), `hud_art=new|classic` and `--hud-art new|classic`: New draws the world art as well. `assets/world_art/` is read at start (a folder that is missing leaves the original's sprites, with a line on stderr). The picture of a whole map (the preview of the setup screen, the thumbnails of the game server) is always the original's look.
- **Sixteen pictures for a plain tile.** The plain gravel, dirt and mud tiles and the bubbles of the mud have sixteen pictures each, so that a field does not repeat. Which one a cell shows is a function of the map as loaded and the cell's own place, in whole-number arithmetic (`world_art::ground_variants`), so every game draws the same: calm pictures (0 to 5) in the open, damp (9 to 11) or stony (6 to 8) beside another kind of ground, worn (12, 13) and cracked (14, 15) in a corridor or a pocket two cells across, stony round the objects, and broad patches of stony, pale and cracked ground. Nothing reads it but the picture.
- **Edges.** Every edge tile of the original's ground has a picture (249 sprites: water and mud both ways, gravel and mud, gravel and dirt, dirt and mud, and slate against the others), drawn so that each piece meets its neighbors in every combination that the maps use.

## The files

`assets/world_art/` holds `world_art.txt` (the manifest) and `sheet_1x.png`, `sheet_2x.png`, `sheet_3x.png`. The format is at the top of [`include/ants_app/world_art.hpp`](../include/ants_app/world_art.hpp): the sheets' sizes, and for each picture the original sprite's number, its variant, the original sprite's size and its place in each sheet; every picture has a ring of one pixel round it that repeats its edge, so that a filtered copy (a window that is not a whole multiple of the canvas) never takes a pixel of its neighbor. The web build packs the folder into its data package with `assets/hud_art/` (`src/ants_app/CMakeLists.txt`, the `Dockerfile`).

## Size

Batch 1 (ground, edges, bridge): 667 pictures; the files are 1.6 MB in all (the 1x sheet 115 KB, 2x 548 KB, 3x 888 KB, the manifest 30 KB), which the web build's data package grows by. In memory a sheet is a texture of its own scale, made when its first picture is drawn: about 3.2 MB at 1x, 11.9 MB at 2x and 26.5 MB at 3x, and only the scales that a window has used are made (a zoomed pass needs the 1x one). No side of a sheet is above 4096.

## What is not in it yet

Dense mud (a big mud field gets a darker, wetter middle, ripples, broader bubbles and color patches that grow with the distance from the banks), the automatic transitions between every pair of kinds of ground, the fire, the bomb, the explosion, the grass, the plants and the hills. A zoomed pass uses the 1x pictures. The Swimmer's and the other ants' own sprites, and the original's objects (stones, food, power-ups) are the original's.

## Tests

`test_world_art` (suite 3.37, [`TESTING_SUITES.md`](TESTING_SUITES.md)) and `tests/scripts/test_world_art_pack.py` ([`TESTING_PYTHON.md`](TESTING_PYTHON.md)); the mutants are `tests/data/world_art_mutants.json`. The golden pictures of the fingerprint suites pin the original's art (their application asks for Classic), which keeps Classic byte for byte. `ANTS_WORLD_ART_SHOTS=FOLDER ./build/tests/test_app/test_world_art` writes the pictures of the batch instead of testing: the real renderer's ground on TREASURE, ISLANDS, a players' map and the bridge's four stages, at 1x, 2x and 3x, the original's beside the new one.
