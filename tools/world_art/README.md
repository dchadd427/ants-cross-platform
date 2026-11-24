# The world art generator

Draws the new ground of the game (the plain tiles, the edge tiles between two kinds of ground, the bridge, the bigger bubbles of the mud) at 1x, 2x and 3x and packs it into the sheets that the game reads. What the game does with them is in [`docs/WORLD_ART.md`](../../docs/WORLD_ART.md); this page is the tool: what it draws, the files it writes and the limits of the pictures.

```
python3 tools/world_art/make_world_art.py            # writes assets/world_art/sheet_1x.png, sheet_2x.png, sheet_3x.png and world_art.txt (about 40 s)
python3 tools/world_art/make_world_art.py --out DIR --work DIR
python3 tools/world_art/mud_reference.py             # writes tests/data/world_art_mud_golden.txt (about 5 s; numpy only), see "The mud's fields and their reference values"
```

Needs Python 3 with Pillow and numpy; nothing else. The libdeflate library is optional: `tools/hud_art/png_write.py` uses it for the sheets when it is installed (5% smaller files, the same pixels). `--out DIR` is the folder of the result (never the map library: `tools/library_guard.py`); `--work DIR` keeps the drawn pictures as `DIR/<scale>x/sprite_NNNN.png`, one file for each picture (without it they go to a temporary folder that is removed at the end). Nothing in the generator names a place on a machine: the repository's root is found from the scripts' own place, and the only inputs are files of the repository: the decoded sprites of `asset_catalog/sprites/` and `docs/chd_table4_animations.json`. The drawing is deterministic: every random choice has a fixed seed, and the same inputs give the same pictures.

On a ten-core Apple machine the drawing takes about 15 s (eight worker processes; about 110 s of processor time in all) and the packing about 20 s, mostly the compression of the three sheets.

## What is in the pack

Batch 1: the ground, its edges and the bridge, and the bigger bubbles of the mud. The numbers are the original's sprite numbers (the bigger bubbles' are made from them, below); the tiles are 32 x 32 world pixels, the bridge's sprites are between 16 and 32 pixels each way. Later batches add the fire, the bomb, the explosion, the grass, the plants and the hills.

| Part | Sprites | Pictures |
|---|---|---|
| Mud and its bubbles | 640 to 657 and 901 | 19 sprites with 16 pictures each (the plain picture and 15 position variants) |
| Gravel | 726, 727 | 2 sprites with 16 pictures each |
| Dirt | 728 to 731 | 4 sprites with 16 pictures each |
| Slate | 812 to 820 | 9 |
| Water | 690 to 693 (the four frames) | 4 |
| Edges | 249 sprites of the tiles `wm`, `MW` (water and mud), `gm`, `mg` (gravel and mud), `gd`, `dg`, `md`, `dm`, `ds`, `sd`, `gs`, `sg`, `ms`, `sm` (the two letters are the two kinds of ground: w water, m mud, g gravel, d dirt, s slate) | 249 |
| Bridge | 112 to 116 | 5 |
| Bigger mud bubbles | the numbers 10641 to 10650 (medium) and 20641 to 20650 (large) | 20 numbers with 16 pictures each (the mud's 16 position variants) |

That is every sprite that the animations 431 to 669 of the table use (the ground), the bridge and the 20 numbers of the bigger bubbles: 312 numbers, 987 pictures at each scale, 675 of them variants. The variants of the gravel are the five kinds of ground that a rule of the game can choose by a cell's surroundings: 0 to 5 calm, 6 to 8 stony, 9 to 11 damp, 12 to 13 worn, 14 to 15 cracked; the mud's and the dirt's variants are sixteen quiet pictures that let a field not repeat.

The bigger bubbles are for the deep middle of a big mud field. Each of the original's ten bubble frames (sprites 641 to 645, one set of bubbles, and 646 to 650, the other) is drawn again on the clean mud of each variant, the same drawing (the same colors, lit side, shaded side and cast shadow; the shadow and the soft edge grow with the bubble) about the point that its set's bubbles grow from, in two sizes: medium (the first set 1.6 times bigger, the second 1.07) and large (2.2 and 1.15). A picture's number is 10000 (medium) or 20000 (large) plus the frame's sprite number, so 10641 to 10650 and 20641 to 20650, each with the variants 0 to 15 and 32 x 32 like the sprite it grows from. No bubble reaches the outermost pixel rows or columns of its tile (`python3 tools/world_art/big_bubbles.py` checks it). Which cell shows which is the game's rule, in [`docs/WORLD_ART.md`](../../docs/WORLD_ART.md).

## How it is drawn

The art is this project's own drawing. It takes the shapes, places and colors of the decoded original sprites as its plan (where a fleck, a streak or a bubble is and how big, the regions of an edge tile and its outline band, the palette, each tile's rhythm) and draws every tile new, in one language: matte, flat colors, hard steps, one light from the upper left (a lit top, a shaded side, a small cast shadow), as quiet as the original. A picture is drawn once on a master of 12 x 12 samples for each world pixel; 3x and 2x are box averages of it, and 1x takes each pixel's most frequent color, so every 1x pixel is a color of the palette (pixel art in the original's colors). The bridge's 1x pictures are the original's own pixels.

| File | What it holds |
|---|---|
| `make_world_art.py` | the command: draws the batch and packs it |
| `draw_all.py` | the list of the pictures and their drawing, in parallel; writes the drawn pictures; stops when the set is not exactly the ground's sprites and the bridge's |
| `catalog.py` | the repository's root and the readers of the decoded sprites and the table |
| `tilekit.py` | the kit of the master: the cubic enlargement of the original's stair-steps into curves, the class map, the dome (a form cut into three hard steps by the light), the three scales of a tile |
| `gravel.py`, `mud_water.py`, `dirt_slate.py` | the plain tiles: the gravel's pebbles (set in CIELAB around the ground's lightness, as the original's flecks are), the clean mud with its bubbles, the water, the dirt, the slate, the bridge |
| `big_bubbles.py` | the bigger bubbles of the mud: the bubble drawing of `mud_water.py` enlarged about its set's center, in the two size classes; its own check that no size reaches the tile's edge |
| `edges.py` | the edge tiles: the banks of water and gravel against mud, the soft edges (gravel and mud against dirt), the rock edges (slate against the others) |
| `pack.py` | the sheets and the manifest |
| `mud_fields.py`, `mud_reference.py` | not part of the pack: the approved math of the dense mud as a standalone port, and the golden file made from it (below) |

## The files

`assets/world_art/` holds four files: the manifest `world_art.txt` (UTF-8, LF, one record a line, nothing else) and the sheets `sheet_1x.png`, `sheet_2x.png` and `sheet_3x.png` (8-bit RGBA PNG).

```
ants-world-art 1
sheet <scale> <png file> <width> <height>                                    three rows: scale 1, 2 and 3; the size of that file in pixels
sprite <number> <variant> <w> <h> <x1> <y1> <x2> <y2> <x3> <y3>              one for each picture, sorted by number and then variant
```

- `number` is the original's sprite number, or for a bigger bubble 10000 (medium) or 20000 (large) plus the bubble sprite's number (10641 to 10650, 20641 to 20650); `variant` is 0 for the plain picture and 1 to 15 for a position variant of a plain tile (640 to 657, 726 to 731 and 901) or of a bigger bubble. `w` and `h` are the ORIGINAL sprite's size at 1x (32 x 32 for a bigger bubble: its sprite's); the picture at scale s is exactly (w x s, h x s). (xs, ys) is the top left pixel of that picture inside the sheet of scale s.
- Every picture is surrounded by a ring of 1 pixel (at every scale) that repeats its own edge pixels, a corner pixel repeating the corner, so that a filtered copy never picks up a neighbor. Pixels stay as drawn, transparent ones with their own color (nothing is premultiplied).
- The sheets are 1024 (1x), 2048 (2x) and 3072 (3x) pixels wide and as high as the shelves need, never over 4096. The layout is deterministic shelf packing: the pictures sorted by height (the tallest first), then number and variant, laid left to right on shelves of their scale's own width, a new shelf when the next picture with its ring does not fit.

`tests/scripts/test_world_art_pack.py` holds the committed files to these rules (it reads them and draws nothing).

## The mud's fields and their reference values

The big mud fields of the game look different in the middle than at the banks (darker and wetter, ripples, color patches, more and bigger bubbles), by a rule of the drawing alone: nothing in the rules of the game reads it. `mud_fields.py` is the approved math of it as a standalone port (numpy only): the depth of a point in tiles from the nearest cell that is not plain mud (it takes a boolean array of the map's cells, True for plain mud; the border of the map is not a bank), the noise (`_hash`, `vnoise`, `fbm`) and the looks of wet, patch and ripple (`layers`, `looks_at`), every number as approved. The game's C++ port is held to it by `tests/data/world_art_mud_golden.txt`, which `mud_reference.py` writes from it: the values of the hash, the noise, the depth and the looks at fixed points of a small synthetic map (24 x 20 cells, all mud but a block of 3 x 3, a column of 12 and a block of 2 x 2) and the median of the ripple field's gradient; its format is the top of `mud_reference.py`. `tests/test_app/test_world_art_mud.cpp` compares the game's port with the file, and `tests/scripts/test_world_art_mud.py` holds the file itself (its format and map, that its sample points reach every depth where a look starts, and that the tool writes it again byte for byte).

## Known limits of the art

- The dirt is the piece the drawing is least sure of: its drifts of darker sand repeat tile after tile, as the original's speckle bands do, and read as soft diagonal stripes at 3x; they are a drawing of the original's rhythm, not its speckle.
- The soft edges (gravel and mud against dirt) are a little rounder than the original's: the contour steps follow the original's boundary, but its small wobbles are smoothed away and a few corners are cut straighter. The slate, the rock edges and the bridge change the least: they are the original with clean curves.
- The original's table has edge tiles only for the pairs listed above. Where a map puts water straight against gravel, dirt or slate, the tiles still meet in a hard line; art for those joins is not in this batch.
- The ring is one pixel, enough for a linear filter on an enlarged copy; a sampler that reads wider (mip levels, a blur) would still reach the neighbor.
- Reproduced pixel for pixel on one machine (macOS, Python 3.14, numpy 2.5, Pillow 12.2). The shapes come from FFT blurs and numpy's random streams, so another platform or a numpy that changes its streams could differ in a few pixels; the committed sheets are the reference. The sheets' bytes also depend on libdeflate being installed (the pixels do not).
