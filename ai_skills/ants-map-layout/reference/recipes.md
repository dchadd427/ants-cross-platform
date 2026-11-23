# Recipes: a whole map in calls

Every block is a call as in `ants-mapmaker-tools/reference/examples.md`. Rows and columns are zero-based. Send `base` (the `version` of your last answer) with each change if the person may be working too.

## Four corners with a lake (60 x 60, a Classic map)

The numbers: size 60. Hill entrances at `a = 8` and `size - 2 - a = 50` (the exact mirror formula, so the map is symmetric). The lake is two overlapping rectangles that are symmetric about the middle (rows 24 to 35, columns 24 to 35). Food 6 cells from each hill, and food 21 cells from two hills at once.

Look first, and start a blank 60 x 60 map if the person wants a new one:

```json
{"tool": "new_map", "args": {"kind": "classic", "rows": 60, "columns": 60, "name": "Four corners"}}
```

The lake, with the ring of mud and the edge pieces (`auto_edges`), as two rectangles:

```json
{"tool": "paint", "args": {"layer": "ground", "tile": 476, "rect": {"row": 24, "column": 26, "rows": 12, "columns": 8}, "auto_edges": true}}
```

```json
{"tool": "paint", "args": {"layer": "ground", "tile": 476, "rect": {"row": 26, "column": 24, "rows": 8, "columns": 12}, "auto_edges": true}}
```

A bridge of finished bridge pieces across the lake, two cells wide:

```json
{"tool": "paint", "args": {"layer": "objects", "tile": 37, "line": {"from": [29, 24], "to": [29, 35], "brush": 2}}}
```

The four hills (entrances):

```json
{"tool": "put_hill", "args": {"team": "green", "row": 8, "column": 8}}
```

```json
{"tool": "put_hill", "args": {"team": "red", "row": 8, "column": 50}}
```

```json
{"tool": "put_hill", "args": {"team": "blue", "row": 50, "column": 8}}
```

```json
{"tool": "put_hill", "args": {"team": "black", "row": 50, "column": 50}}
```

Six ants for each color, five cells from the entrance toward the middle:

```json
{"tool": "put_ants", "args": {"team": "green", "row": 13, "column": 13, "count": 6}}
```

```json
{"tool": "put_ants", "args": {"team": "red", "row": 13, "column": 45, "count": 6}}
```

```json
{"tool": "put_ants", "args": {"team": "blue", "row": 45, "column": 13, "count": 6}}
```

```json
{"tool": "put_ants", "args": {"team": "black", "row": 45, "column": 45, "count": 6}}
```

A safe pile for each color (6 cells from its hill):

```json
{"tool": "put_food", "args": {"row": 14, "column": 8, "stages": 4, "bites": 30, "points": 25}}
```

```json
{"tool": "put_food", "args": {"row": 14, "column": 50, "stages": 4, "bites": 30, "points": 25}}
```

```json
{"tool": "put_food", "args": {"row": 44, "column": 8, "stages": 4, "bites": 30, "points": 25}}
```

```json
{"tool": "put_food", "args": {"row": 44, "column": 50, "stages": 4, "bites": 30, "points": 25}}
```

Contested piles between neighbors (21 cells from two hills each), richer:

```json
{"tool": "put_food", "args": {"row": 8, "column": 29, "stages": 5, "bites": 40, "points": 30}}
```

```json
{"tool": "put_food", "args": {"row": 29, "column": 8, "stages": 5, "bites": 40, "points": 30}}
```

```json
{"tool": "put_food", "args": {"row": 29, "column": 50, "stages": 5, "bites": 40, "points": 30}}
```

```json
{"tool": "put_food", "args": {"row": 50, "column": 29, "stages": 5, "bites": 40, "points": 30}}
```

Power-ups in a symmetric set: combat ones at the four corners of the lake's ring, swimmer ones on the two shores that the bridge joins:

```json
{"tool": "paint", "args": {"layer": "objects", "tile": 62, "cells": [[22, 22], [22, 37], [37, 22], [37, 37]]}}
```

```json
{"tool": "paint", "args": {"layer": "objects", "tile": 65, "cells": [[29, 22], [29, 37]]}}
```

Two flowers that drop power-ups every 30 seconds, mirrored:

```json
{"tool": "put_plant", "args": {"tile": 421, "row": 29, "column": 12, "dropper": 30}}
```

```json
{"tool": "put_plant", "args": {"tile": 421, "row": 29, "column": 47, "dropper": 30}}
```

The properties, the edges, the check and a copy:

```json
{"tool": "set_properties", "args": {"description": "Four corners, one lake", "minutes": 10, "eggs": 6}}
```

```json
{"tool": "fix_edges", "args": {}}
```

```json
{"tool": "check_map", "args": {}}
```

```json
{"tool": "save_draft", "args": {"name": "Four corners"}}
```

Then look with `get_picture`, read the check, and walk the paths with the region text (`ants-map-check-and-fix`).

## Close quarters (TREASURE-like, 60 x 60)

The same calls with these changes: hill entrances at `a = 21` and `size - 2 - a = 37` (neighbors 16 apart); six ants five cells from each entrance toward the middle; 14 to 18 piles of 10 to 60 bites in a ring round the four hills (a safe pile 5 to 7 cells from each hill, the rest in pairs between neighbors); rocks and bigger objects (`put_object`) in lines with gaps two cells wide; no lake, or a small pond; 12 minutes, 9 eggs.

## Islands (60 x 60)

Each hill on its own island of ground about 16 x 16, a channel of water 4 to 6 cells wide between islands, mud on the shores (`auto_edges`), one bridge between each pair of neighbors, swimmer power-ups on the shores, bomber power-ups on the bridges' ends, about ten piles on the islands, 12 minutes.
