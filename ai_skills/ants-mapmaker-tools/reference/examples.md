# One valid call for each tool

Each block is the body of `POST /hub/ai/v1/call` (an MCP client sends the same `name` and `arguments`). Rows and columns are zero-based. `base` is the `version` of the answer you got last.

Looking:

```json
{"tool": "map_info", "args": {}}
```

```json
{"tool": "get_region", "args": {"row": 0, "column": 0, "rows": 30, "columns": 30}}
```

```json
{"tool": "get_picture", "args": {"max_px": 800}}
```

```json
{"tool": "check_map", "args": {}}
```

```json
{"tool": "list_tiles", "args": {"group": "ground"}}
```

```json
{"tool": "read_guide", "args": {"topic": "layout"}}
```

```json
{"tool": "list_library_maps", "args": {}}
```

```json
{"tool": "view_library_map", "args": {"name": "TREASURE", "view": "region", "row": 0, "column": 0, "rows": 60, "columns": 60}}
```

Changing:

```json
{"tool": "paint", "args": {"layer": "ground", "tile": 476, "rect": {"row": 26, "column": 26, "rows": 8, "columns": 8}, "auto_edges": true, "base": 3}}
```

```json
{"tool": "paint", "args": {"layer": "ground", "tile": 439, "line": {"from": [10, 5], "to": [10, 40], "brush": 2}}}
```

```json
{"tool": "paint", "args": {"layer": "objects", "tile": 302, "cells": [[5, 5], [5, 6], [6, 5]]}}
```

```json
{"tool": "paint", "args": {"layer": "ground", "tile": 487, "fill": {"row": 3, "column": 3}}}
```

```json
{"tool": "put_hill", "args": {"team": "green", "row": 8, "column": 8}}
```

```json
{"tool": "put_ants", "args": {"team": "green", "row": 12, "column": 12, "count": 6}}
```

```json
{"tool": "put_food", "args": {"row": 14, "column": 8, "stages": 4, "bites": 30, "points": 25}}
```

```json
{"tool": "put_plant", "args": {"tile": 421, "row": 20, "column": 20, "dropper": 30}}
```

```json
{"tool": "put_object", "args": {"tile": 1323, "row": 15, "column": 15}}
```

A field of bombs on every other cell of a box, and a meadow of flowers on a tenth of another one (the same `seed` is the same meadow):

```json
{"tool": "fill_area", "args": {"rect": {"row": 20, "column": 20, "rows": 8, "columns": 12}, "object": 129, "pattern": "checker"}}
```

```json
{"tool": "fill_area", "args": {"rect": {"row": 30, "column": 5, "rows": 10, "columns": 10}, "plant": {"tile": 421}, "pattern": "random", "density": 10, "seed": 7}}
```

```json
{"tool": "erase", "args": {"rect": {"row": 0, "column": 0, "rows": 4, "columns": 4}}}
```

```json
{"tool": "set_properties", "args": {"description": "Lakes and bridges", "minutes": 10, "eggs": 6}}
```

```json
{"tool": "fix_edges", "args": {}}
```

```json
{"tool": "resize_map", "args": {"rows": 60, "columns": 60}}
```

```json
{"tool": "new_map", "args": {"kind": "classic", "rows": 60, "columns": 60, "name": "Lakes"}}
```

```json
{"tool": "undo", "args": {"steps": 1}}
```

```json
{"tool": "redo", "args": {"steps": 1}}
```

```json
{"tool": "save_draft", "args": {"name": "Lakes, first try"}}
```

```json
{"tool": "note", "args": {"text": "Now the four hills."}}
```
