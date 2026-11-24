# The world art generator

Draws the new ground of the game (the plain tiles, the edge tiles between two kinds of ground, the bridge) at 1x, 2x and 3x and packs it into the sheets that the game reads. What the game does with them is in [`docs/WORLD_ART.md`](../../docs/WORLD_ART.md); this page is the tool: what it draws, the files it writes and the limits of the pictures.

```
python3 tools/world_art/make_world_art.py            # writes assets/world_art/sheet_1x.png, sheet_2x.png, sheet_3x.png and world_art.txt (about half a minute)
python3 tools/world_art/make_world_art.py --out DIR --work DIR
```

Needs Python 3 with Pillow and numpy; nothing else. The libdeflate library is optional: `tools/hud_art/png_write.py` uses it for the sheets when it is installed (5% smaller files, the same pixels). `--out DIR` is the folder of the result (never the map library: `tools/library_guard.py`); `--work DIR` keeps the drawn pictures as `DIR/<scale>x/sprite_NNNN.png`, one file for each picture (without it they go to a temporary folder that is removed at the end). Nothing in the generator names a place on a machine: the repository's root is found from the scripts' own place, and the only inputs are files of the repository: the decoded sprites of `asset_catalog/sprites/` and `docs/chd_table4_animations.json`. The drawing is deterministic: every random choice has a fixed seed, and the same inputs give the same pictures.

On a ten-core Apple machine the drawing takes about 10 s (eight worker processes; about 45 s on one core) and the packing about 15 s, mostly the compression of the three sheets.

## What is in the pack

Batch 1: the ground, its edges and the bridge. The numbers are the original's sprite numbers; the tiles are 32 x 32 world pixels, the bridge's sprites are between 16 and 32 pixels each way. Later batches add the fire, the bomb, the explosion, the grass, the plants and the hills.

| Part | Sprites | Pictures |
|---|---|---|
| Mud and its bubbles | 640 to 657 and 901 | 19 sprites with 16 pictures each (the plain picture and 15 position variants) |
| Gravel | 726, 727 | 2 sprites with 16 pictures each |
| Dirt | 728 to 731 | 4 sprites with 16 pictures each |
| Slate | 812 to 820 | 9 |
| Water | 690 to 693 (the four frames) | 4 |
| Edges | 249 sprites of the tiles `wm`, `MW` (water and mud), `gm`, `mg` (gravel and mud), `gd`, `dg`, `md`, `dm`, `ds`, `sd`, `gs`, `sg`, `ms`, `sm` (the two letters are the two kinds of ground: w water, m mud, g gravel, d dirt, s slate) | 249 |
| Bridge | 112 to 116 | 5 |

That is every sprite that the animations 431 to 669 of the table use (the ground), and the bridge: 292 sprites, 667 pictures at each scale, 375 of them variants. The variants of the gravel are the five kinds of ground that a rule of the game can choose by a cell's surroundings: 0 to 5 calm, 6 to 8 stony, 9 to 11 damp, 12 to 13 worn, 14 to 15 cracked; the mud's and the dirt's variants are sixteen quiet pictures that let a field not repeat.

## How it is drawn

The art is this project's own drawing. It takes the shapes, places and colors of the decoded original sprites as its plan (where a fleck, a streak or a bubble is and how big, the regions of an edge tile and its outline band, the palette, each tile's rhythm) and draws every tile new, in one language: matte, flat colors, hard steps, one light from the upper left (a lit top, a shaded side, a small cast shadow), as quiet as the original. A picture is drawn once on a master of 12 x 12 samples for each world pixel; 3x and 2x are box averages of it, and 1x takes each pixel's most frequent color, so every 1x pixel is a color of the palette (pixel art in the original's colors). The bridge's 1x pictures are the original's own pixels.

| File | What it holds |
|---|---|
| `make_world_art.py` | the command: draws the batch and packs it |
| `draw_all.py` | the list of the pictures and their drawing, in parallel; writes the drawn pictures; stops when the set is not exactly the ground's sprites and the bridge's |
| `catalog.py` | the repository's root and the readers of the decoded sprites and the table |
| `tilekit.py` | the kit of the master: the cubic enlargement of the original's stair-steps into curves, the class map, the dome (a form cut into three hard steps by the light), the three scales of a tile |
| `gravel.py`, `mud_water.py`, `dirt_slate.py` | the plain tiles: the gravel's pebbles (set in CIELAB around the ground's lightness, as the original's flecks are), the clean mud with its bubbles, the water, the dirt, the slate, the bridge |
| `edges.py` | the edge tiles: the banks of water and gravel against mud, the soft edges (gravel and mud against dirt), the rock edges (slate against the others) |
| `pack.py` | the sheets and the manifest |

## The files

`assets/world_art/` holds four files: the manifest `world_art.txt` (UTF-8, LF, one record a line, nothing else) and the sheets `sheet_1x.png`, `sheet_2x.png` and `sheet_3x.png` (8-bit RGBA PNG).

```
ants-world-art 1
sheet <scale> <png file> <width> <height>                                    three rows: scale 1, 2 and 3; the size of that file in pixels
sprite <number> <variant> <w> <h> <x1> <y1> <x2> <y2> <x3> <y3>              one for each picture, sorted by number and then variant
```

- `number` is the original's sprite number, `variant` is 0 for the plain picture and 1 to 15 for a position variant of a plain tile (640 to 657, 726 to 731 and 901). `w` and `h` are the ORIGINAL sprite's size at 1x; the picture at scale s is exactly (w x s, h x s). (xs, ys) is the top left pixel of that picture inside the sheet of scale s.
- Every picture is surrounded by a ring of 1 pixel (at every scale) that repeats its own edge pixels, a corner pixel repeating the corner, so that a filtered copy never picks up a neighbor. Pixels stay as drawn, transparent ones with their own color (nothing is premultiplied).
- The sheets are 1024 (1x), 2048 (2x) and 3072 (3x) pixels wide and as high as the shelves need, never over 4096. The layout is deterministic shelf packing: the pictures sorted by height (the tallest first), then number and variant, laid left to right on shelves of their scale's own width, a new shelf when the next picture with its ring does not fit.

`tests/scripts/test_world_art_pack.py` holds the committed files to these rules (it reads them and draws nothing).

## Known limits of the art

- The dirt is the piece the drawing is least sure of: its drifts of darker sand repeat tile after tile, as the original's speckle bands do, and read as soft diagonal stripes at 3x; they are a drawing of the original's rhythm, not its speckle.
- The soft edges (gravel and mud against dirt) are a little rounder than the original's: the contour steps follow the original's boundary, but its small wobbles are smoothed away and a few corners are cut straighter. The slate, the rock edges and the bridge change the least: they are the original with clean curves.
- The original's table has edge tiles only for the pairs listed above. Where a map puts water straight against gravel, dirt or slate, the tiles still meet in a hard line; art for those joins is not in this batch.
- The ring is one pixel, enough for a linear filter on an enlarged copy; a sampler that reads wider (mip levels, a blur) would still reach the neighbor.
- Reproduced pixel for pixel on one machine (macOS, Python 3.14, numpy 2.5, Pillow 12.2). The shapes come from FFT blurs and numpy's random streams, so another platform or a numpy that changes its streams could differ in a few pixels; the committed sheets are the reference. The sheets' bytes also depend on libdeflate being installed (the pixels do not).
