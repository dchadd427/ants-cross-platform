# The HUD art generator

Draws the new pictures of the match screen's HUD (the frame, the buttons, the pedestals with their pictures, Stop, the egg tray, the lunch box) and packs them into the sheets that the game reads. What the game does with them, the setting that chooses them and the files' format are in [`docs/VIEW_AND_HUD.md`](../../docs/VIEW_AND_HUD.md#hud-art) and at the top of `include/ants_app/hud_art.hpp`.

```
python3 tools/hud_art/make_hud_art.py            # writes assets/hud_art/sheet_1x.png, sheet_2x.png, sheet_3x.png and hud_art.txt (about 30 seconds)
```

Needs Python 3 with Pillow and numpy, and Google Chrome or Chromium (headless: it renders the SVG pieces; `--chrome PROGRAM` or `ANTS_CHROME` names it when it is not in the usual place). `--work DIR` (or `ANTS_HUD_ART_WORK`) is the folder of the scratch files, `--out DIR` the folder of the result. Nothing in the generator names a place on a machine: the repository's root is found from the scripts' own place. The drawing is deterministic: the same inputs give the same files.

## What is here

| File | What it holds |
|---|---|
| `make_hud_art.py` | the command: draws the catalog at 1x, 2x and 3x and packs it |
| `sprites.py` | the catalog: every sprite, its place at 960 x 540, its anchor and slice, and the animations of the original that the sprites replace (names and frame times come from `docs/chd_table4_animations.json` and `asset_catalog/`) |
| `pack.py` | the sheets and the manifest (`hud_art.txt`; its format is described at the top of the file); skyline packing, one cell for each different picture, each sprite's edge repeated round it |
| `pieces.py`, `system.py`, `texture.py`, `ants.py`, `lettering.py` | the drawn pieces as SVG: the bars, strips, panel, windows, buttons and card, the one bevel system, the plate's texture tile, the ornament ants, the labels |
| `relief.py`, `carve.py` | the carved pieces, built as height fields and lit from the top left: the pedestals and their eight pictures, the gold band and the rim flash, the socket, the eggs, the lunch box |
| `orig_anim.py`, `cat_orig.py` | read the original's animation names, times and places from the repository's decoded catalog (the layout numbers only: no picture of the original is used) |
| `glyphs.json`, `label_boxes.json` | the outlines of the small symbols, and where each label's letters stand |
| `reference_frame.py` | composes the frame at one shape and scale from the sheets by the manifest's rules alone; `tests/data/hud_art/` holds its output at 16:9 and 20:9 (1x), which `test_hud_art` compares with the real renderer |
| `fonts/` | the two typefaces that set the labels, with their licences (below) |

The reference pictures are made again with `python3 tools/hud_art/reference_frame.py 960 540 --out tests/data/hud_art/frame_16x9_1x.png` (and `1200 540`, `frame_20x9_1x.png`). They change only when the art does.

## Fonts

The labels are set in **Libre Baskerville Regular** (`fonts/LibreBaskerville-Regular.ttf`, licence `fonts/LibreBaskerville-OFL.txt`) and the word Chat in **Lora Bold Italic** (`fonts/Lora-BoldItalic.ttf`, licence `fonts/Lora-OFL.txt`), both under the SIL Open Font License 1.1. They are used here, at drawing time, and nowhere else: the letters are baked into the sprites, so the game ships no font file for them. The licence texts stay with the fonts ([`THIRD_PARTY_NOTICES.md`](../../THIRD_PARTY_NOTICES.md)).

## Known limits of the art

- The lunch box is a front view; the original's is a three-quarter view of a pail.
- The latch has no gold side walls inside its band, the seat in the rise and the swap is shallow, and the turning slab is flat.
- At 1x the carvings are averaged from a larger render: softer than hand-placed pixels. The ornament ants' legs are faint.
- The plate texture's phase jumps where a right- or bottom-anchored piece meets a repeated run at a width that is not 960 + n x 192 or a height that is not 540 + n x 128.
- The dialogs' frame on clay (quit, options, quick help, results) is not drawn: the match screen does not use it.
