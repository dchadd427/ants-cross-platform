# The map maker (the editor page)

A page of the site, `mapmaker.html`, where a player makes a map, opens one from their own computer, looks at it, checks it and saves it. It is built on the map core that runs in the browser ([`MAP_FORMAT.md`](MAP_FORMAT.md), "In the browser"): the page never judges a map itself, it shows what the core answers (the check, the limits, the refusal of a size) in the core's own sentences. This page says what the editor is, what each of its pull requests does and which are done.

## The rules of the editor

- **Two kinds of map.** A **Classic map** works in the original game and in the new version (a new one has at most 90 rows, 130 columns and 10,000 cells). An **Extended map** works in the new version only (at most 512 x 512 cells, and no side longer than 1,024); it wears the quiet blue-grey badge "New version only", which is a note and not a fault. The numbers on screen are the core's (`limits()`), never a copy in the page.
- **Nothing is ever written over.** Making a copy, starting an Extended map from a Classic one (any Classic map, the game's library maps too) and saving always make a **new** map under a name that is free; the name of a library map or of a draft that exists counts as taken. The page has no way to open a file for writing: the only thing that leaves it is a download, which the browser saves as a new file.
- **Everything stays on the player's own device.** From E5 on, a draft is kept in the browser; the finished file is downloaded (E1 already does that). Sharing a map with other players is a separate, later piece of work and is not part of the editor.
- **Rows and columns count from 0**, as in the checker's sentences ("row 12, column 40"), so a "Show me" link and the position line say the same numbers.

## The pull requests

| | What | State |
|---|---|---|
| E1 | The page and its shell (top bar, tools column, canvas, side panel, dialogs, the three layouts: desktop, phone upright, phone sideways); New map (the two kinds, size boxes with the live limits and file size, the core's refusals); Open a `.lvl` from the disk (kind and check shown); the canvas with pan and zoom (mouse drag and wheel, one finger and two fingers, the keyboard), a small map of the whole map while only a part is on screen, and drawing of the visible tiles only (measured in headless Chromium on a development machine: about 15 ms for a wheel step and its frame over the whole 512 x 512 map); the checks card for each kind; Download. The page refuses a map core of another interface version. | done |
| E2 | Tools for the two layers: paint, erase and fill for ground and objects, with undo and redo kept as small patches (the cells that changed, never a copy of the map). The anchors of objects follow the rules of version 9 when a map grows. | planned |
| E3 | Hills, starting ants, food piles and the properties of the map (description, minutes, eggs for each team, default ant type), each checked by the core as it is placed. | planned |
| E4 | Convert: "Start an Extended map from this one" (always a new map), "Make a Classic map" when it fits, and when it does not the plain list of reasons with "Crop it to fit" (a movable frame, never more than the limits of a new Classic map) and "Shorten the description". | planned |
| E5 | Drafts and "Save as a new map": the map's bytes will be kept in the browser's own database (IndexedDB) with autosave, working without it when the browser refuses; the name check (library names and existing drafts are taken; the next free name is offered); the Copy dialog. | planned |
| E6 | "My maps": the list of the player's drafts with their kind tags, and deleting one. | planned |

The lobby's map list and the match page's "Download this map" show the kind of a map too; that is the work of the changes that load Extended maps in the game, not of the editor.

## How the page is made

All of it is static files: `web/mapmaker.html` and, in `web/front/`, `mapmaker.css` and the scripts below (each a plain script that a page and node can load). The page loads the core's `lvl_core.js` and `lvl_core_api.js` first and the app last.

| File | What it does |
|---|---|
| `mapmaker_file.js` | Reads the grid and the markers out of the bytes of a file (version 8 and version 9) for drawing, where they are, without copying the cells; names a download. It judges nothing. |
| `mapmaker_tiles.js` | The color that stands for a tile: a few kinds of ground and of object, and the four teams. They are not the game's pictures (those come later). |
| `mapmaker_view.js` | Where the map is on the screen (the point in the middle and the size of a tile): pan, zoom, the visible tiles, and the drawing: one rectangle for each visible tile, or, when a tile is under 5 pixels, one lookup for each pixel of the canvas. The small map is made once for a file. |
| `mapmaker_sizes.js` | The limits as the cards say them, the live line (cells and file size), and the core's refusal of a size turned into the sentence of the picture. |
| `mapmaker_checks.js` | The checks card: the core's `checkMap` answer, with its sentences shown as text. |
| `mapmaker_dialog.js` | The dialog box (focus, Escape, the page behind it inert) and the dialogs: New map, checks, messages, the phone's menu. |
| `mapmaker_canvas.js` | The hands: pointer, wheel, keyboard, the zoom buttons, the small map. |
| `mapmaker_app.js` | Starts the core, holds the open map, joins the parts. |

A map is read as a place that takes both kinds reads it (the core's profile `extended`), so a library map and an Extended map open alike; the limits of a **new** Classic map are those of the profile `new_map`.

## Tests

`tests/scripts/test_web_mapmaker.py` (quick tier) runs `tests/scripts/web_mapmaker_check.js` (the reader on all 540 maps of the library, the view, the sizes, the card, the colors; broken copies of the scripts must fail it) and pins the page's wiring. `tests/scripts/web_mapmaker_dialog_check.js` runs the dialogs on a small fake of the DOM (focus, Escape, Tab, New map; broken copies must fail it). The canvas and the page's start need a real browser: the opt-in check covers them. `tests/scripts/web_mapmaker_core_check.js` holds the page's reading to the real module (the web job of CI runs it from the image). The opt-in browser check is `tests/scripts/test_web_mapmaker.sh` ([`TESTING.md`](TESTING.md)).
