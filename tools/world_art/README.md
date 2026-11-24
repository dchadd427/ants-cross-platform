# The world art generator

Draws the new ground of the game (the plain tiles, the edge tiles between two kinds of ground, the bridge, the bigger bubbles of the mud) and what lies on it (the four teams' bombs, the bomb's explosion, the fire wall) at 1x, 2x and 3x and packs it into the sheets that the game reads. What the game does with them is in [`docs/WORLD_ART.md`](../../docs/WORLD_ART.md); this page is the tool: what it draws, the files it writes and the limits of the pictures.

```
python3 tools/world_art/make_world_art.py            # writes assets/world_art/sheet_1x.png, sheet_2x.png, sheet_3x.png and world_art.txt (about 45 s)
python3 tools/world_art/make_world_art.py --out DIR --work DIR
python3 tools/world_art/mud_reference.py             # writes tests/data/world_art_mud_golden.txt (about 5 s; numpy only), see "The mud's fields and their reference values"
```

Needs Python 3 with Pillow and numpy; nothing else. The libdeflate library is optional: `tools/hud_art/png_write.py` uses it for the sheets when it is installed (5% smaller files, the same pixels). `--out DIR` is the folder of the result (never the map library: `tools/library_guard.py`); `--work DIR` keeps the drawn pictures as `DIR/<scale>x/sprite_NNNN.png`, one file for each picture (without it they go to a temporary folder that is removed at the end). Nothing in the generator names a place on a machine: the repository's root is found from the scripts' own place, and the only inputs are files of the repository: the decoded sprites of `asset_catalog/sprites/` and `docs/chd_table4_animations.json`. The drawing is deterministic: every random choice has a fixed seed, and the same inputs give the same pictures.

On a ten-core Apple machine the drawing takes about 20 s (eight worker processes; about 150 s of processor time in all, 20 of them for the explosion) and the packing about 25 s, mostly the compression of the three sheets.

## What is in the pack

Batches 1 and 1b: the ground, its edges and the bridge, and the bigger bubbles of the mud. Batch 3: the bombs, the explosion and the fire wall. The numbers are the original's sprite numbers (the bigger bubbles', the explosion's light's and the fire wall's are made from them, below); the tiles are 32 x 32 world pixels, the bridge's sprites are between 16 and 32 pixels each way. Later batches add the grass, the plants and the hills.

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
| Bombs | 298 to 305 | 8 (the red, green, black and blue bomb, two frames each: the fuse not alight and alight) |
| Explosion | 306 to 315 | 10 (the ten frames of the blast, smoke and fire) |
| Explosion's light | the numbers 30306 to 30315 | 10 (the same frames' fire alone) |
| Fire wall | the numbers 50316 to 50320 | 5 (the wall's five frames) |

That is every sprite that the animations 431 to 669 of the table use (the ground), the bridge, the 20 numbers of the bigger bubbles, the bombs, the explosion with its light and the fire wall: 345 numbers, 1,020 pictures at each scale, 675 of them variants. The variants of the gravel are the five kinds of ground that a rule of the game can choose by a cell's surroundings: 0 to 5 calm, 6 to 8 stony, 9 to 11 damp, 12 to 13 worn, 14 to 15 cracked; the mud's and the dirt's variants are sixteen quiet pictures that let a field not repeat.

The bombs, the explosion and the fire wall are this project's own drawings too. The bomb is the original's canister drawn again in the game's own view: a short drum of charge with a flat round top standing in an open iron cup, the team's color a band round the cup, one light from the upper left, a thin dark outline; solid, and 12 x 24 world pixels like the sprite. The fire wall and the explosion are drawn as light: the original's shapes, flecks and frames kept, no outline, see-through by heat. The fire wall's opacity goes by the original's level of each pixel (the white core 0.98, the cool red-orange rim 0.30), its edge fades out over about a pixel, the coals lie under it as one ash bed. The explosion's fireball has six color steps from red to a white core (opacity 0.72 to 0.98) and its smoke is a matte veil in the original's three tones (opacity 0.72 to 0.85), fading out at its ragged edge. Each of the ten frames has a second picture, its light: the fireball alone, for a pass that adds it to the ground as light (the last four frames are smoke only, so their light is a picture with nothing in it). The fire wall is numbered 50000 plus its sprite, not by the sprite's own number, because the original's sprites 316 to 319 are also parts of the Fire ant's own animations, which keep the original's pictures. Every one of these pictures has its own alpha, which stays exactly as drawn (a picture of the explosion, the light or the fire wall has no opaque pixel).

The bigger bubbles are for the deep middle of a big mud field. Each of the original's ten bubble frames (sprites 641 to 645, one set of bubbles, and 646 to 650, the other) is drawn again on the clean mud of each variant, the same drawing (the same colors, lit side, shaded side and cast shadow; the shadow and the soft edge grow with the bubble) about the point that its set's bubbles grow from, in two sizes: medium (the first set 1.6 times bigger, the second 1.07) and large (2.2 and 1.15). A picture's number is 10000 (medium) or 20000 (large) plus the frame's sprite number, so 10641 to 10650 and 20641 to 20650, each with the variants 0 to 15 and 32 x 32 like the sprite it grows from. No bubble reaches the outermost pixel rows or columns of its tile (`python3 tools/world_art/big_bubbles.py` checks it). Which cell shows which is the game's rule, in [`docs/WORLD_ART.md`](../../docs/WORLD_ART.md).

## How it is drawn

The art is this project's own drawing. It takes the shapes, places and colors of the decoded original sprites as its plan (where a fleck, a streak or a bubble is and how big, the regions of an edge tile and its outline band, the palette, each tile's rhythm) and draws every tile new, in one language: matte, flat colors, hard steps, one light from the upper left (a lit top, a shaded side, a small cast shadow), as quiet as the original. A picture is drawn once on a master of 12 x 12 samples for each world pixel; 3x and 2x are box averages of it, and 1x takes each pixel's most frequent color, so every 1x pixel is a color of the palette (pixel art in the original's colors). The bridge's 1x pictures are the original's own pixels.

The bombs, the explosion and the fire wall are drawn on masters with an alpha instead: color is kept multiplied by its opacity while it is painted (`canvas.py`), so that the average of a see-through edge has no dark fringe; 3x and 2x are averages of it, with the opacity rounded to a whole number of 255 and the color divided by it again. The bomb's master is 6 samples for each world pixel (its 1x is an average too, so its outline at 1x has blended pixels); the fire wall's is 12, and its 1x is the original's own pixels, each with its level's color and opacity (every 1x pixel a color of the palette); the explosion's is 12 samples with a margin of 4 world pixels that is cut off again, in single precision as it was approved, and its 1x is the original's pixels with their class's color and opacity.

| File | What it holds |
|---|---|
| `make_world_art.py` | the command: draws the batch and packs it |
| `draw_all.py` | the list of the pictures and their drawing, in parallel; writes the drawn pictures; stops when the set is not exactly the ground's sprites, the bridge's, the bigger bubbles', the bombs', the explosion's with its light and the fire wall's, or a picture is not its sprite's size times its scale |
| `catalog.py` | the repository's root and the readers of the decoded sprites and the table |
| `tilekit.py` | the kit of the master: the cubic enlargement of the original's stair-steps into curves, the class map, the dome (a form cut into three hard steps by the light), the three scales of a tile |
| `gravel.py`, `mud_water.py`, `dirt_slate.py` | the plain tiles: the gravel's pebbles (set in CIELAB around the ground's lightness, as the original's flecks are), the clean mud with its bubbles, the water, the dirt, the slate, the bridge |
| `big_bubbles.py` | the bigger bubbles of the mud: the bubble drawing of `mud_water.py` enlarged about its set's center, in the two size classes; its own check that no size reaches the tile's edge |
| `edges.py` | the edge tiles: the banks of water and gravel against mud, the soft edges (gravel and mud against dirt), the rock edges (slate against the others) |
| `canvas.py` | the canvas of the things on the ground: a master of 6 or 12 samples for each world pixel with color multiplied by opacity, painted in layers and averaged down to a scale |
| `bomb.py`, `fire.py`, `explosion.py` | the bombs of the four teams (6 samples, solid), the fire wall (the original's levels read as a map, drawn as light, opacity by heat) and the explosion with its light (classes read from the original's frames, drawn as light with its smoke as a veil) |
| `pack.py` | the sheets and the manifest |
| `mud_fields.py`, `mud_reference.py` | not part of the pack: the approved math of the dense mud as a standalone port, and the golden file made from it (below) |

## The files

`assets/world_art/` holds four files: the manifest `world_art.txt` (UTF-8, LF, one record a line, nothing else) and the sheets `sheet_1x.png`, `sheet_2x.png` and `sheet_3x.png` (8-bit RGBA PNG).

```
ants-world-art 1
sheet <scale> <png file> <width> <height>                                    three rows: scale 1, 2 and 3; the size of that file in pixels
sprite <number> <variant> <w> <h> <x1> <y1> <x2> <y2> <x3> <y3>              one for each picture, sorted by number and then variant
```

- `number` is the original's sprite number, or for a bigger bubble 10000 (medium) or 20000 (large) plus the bubble sprite's number (10641 to 10650, 20641 to 20650), for an explosion frame's light 30000 plus the frame's sprite number (30306 to 30315), for the fire wall 50000 plus the sprite's number (50316 to 50320); `variant` is 0 for the plain picture and 1 to 15 for a position variant of a plain tile (640 to 657, 726 to 731 and 901) or of a bigger bubble. `w` and `h` are the ORIGINAL sprite's size at 1x (32 x 32 for a bigger bubble: its sprite's; the explosion's light and the fire wall are the size of the sprite their number is made from); the picture at scale s is exactly (w x s, h x s). (xs, ys) is the top left pixel of that picture inside the sheet of scale s.
- Every picture is surrounded by a ring of 1 pixel (at every scale) that repeats its own edge pixels, a corner pixel repeating the corner, so that a filtered copy never picks up a neighbor. Pixels stay as drawn, transparent ones with their own color (nothing is premultiplied), and the alpha of a see-through picture stays exactly as drawn, a partly transparent edge pixel repeated in the ring with its color and its opacity.
- The sheets are 1024 (1x), 2048 (2x) and 3072 (3x) pixels wide and as high as the shelves need, never over 4096. The layout is deterministic shelf packing: the pictures sorted by height (the tallest first), then number and variant, laid left to right on shelves of their scale's own width, a new shelf when the next picture with its ring does not fit.

`tests/scripts/test_world_art_pack.py` holds the committed files to these rules (it reads them and draws nothing).

## The mud's fields and their reference values

The big mud fields of the game look different in the middle than at the banks (darker and wetter, ripples, color patches, more and bigger bubbles), by a rule of the drawing alone: nothing in the rules of the game reads it. `mud_fields.py` is the approved math of it as a standalone port (numpy only): the depth of a point in tiles from the nearest cell that is not plain mud (it takes a boolean array of the map's cells, True for plain mud; the border of the map is not a bank), the noise (`_hash`, `vnoise`, `fbm`) and the looks of wet, patch and ripple (`layers`, `looks_at`), every number as approved. The game's C++ port is held to it by `tests/data/world_art_mud_golden.txt`, which `mud_reference.py` writes from it: the values of the hash, the noise, the depth and the looks at fixed points of a small synthetic map (24 x 20 cells, all mud but a block of 3 x 3, a column of 12 and a block of 2 x 2) and the median of the ripple field's gradient; its format is the top of `mud_reference.py`. `tests/test_app/test_world_art_mud.cpp` compares the game's port with the file, and `tests/scripts/test_world_art_mud.py` holds the file itself (its format and map, that its sample points reach every depth where a look starts, and that the tool writes it again byte for byte).

## Known limits of the art

- The dirt is the piece the drawing is least sure of: its drifts of darker sand repeat tile after tile, as the original's speckle bands do, and read as soft diagonal stripes at 3x; they are a drawing of the original's rhythm, not its speckle.
- The soft edges (gravel and mud against dirt) are a little rounder than the original's: the contour steps follow the original's boundary, but its small wobbles are smoothed away and a few corners are cut straighter. The slate, the rock edges and the bridge change the least: they are the original with clean curves.
- The original's table has edge tiles only for the pairs listed above. Where a map puts water straight against gravel, dirt or slate, the tiles still meet in a hard line; art for those joins is not in this batch.
- The ring is one pixel, enough for a linear filter on an enlarged copy; a sampler that reads wider (mip levels, a blur) would still reach the neighbor.
- The pictures of the fire wall, the explosion and its light are made to be drawn as light (blended by their own opacity and then added to the ground once more, over a warm glow); drawn by plain alpha blending alone they are dimmer than approved.
- The pictures keep straight (not premultiplied) alpha. A picture's fully transparent pixels are black (a few at a soft edge, whose opacity rounds to nothing, keep the edge's color), and a bilinear filter on a picture enlarged past 1x can blend that black into a see-through edge as a faint dark fringe; the ring repeats the edge pixel, not a color to blend toward, so it does not help against it.
- Reproduced pixel for pixel on one machine (macOS, Python 3.14, numpy 2.5, Pillow 12.2). The shapes come from FFT blurs and numpy's random streams, and the explosion's smoke is uneven by a value noise from numpy's stream enlarged by Pillow's bicubic resize, in single precision, so another platform or a numpy or Pillow that changes its streams or its resize could differ in a few pixels; the committed sheets are the reference. The sheets' bytes also depend on libdeflate being installed (the pixels do not).
