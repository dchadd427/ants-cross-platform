# Asset catalog and inspector

A web page that shows every sprite, sound and animation that the game reads from the archive `Original-Ants/ants.chd`, as files you can look at, play and download. It is one static page (`asset_catalog/index.html`) with its data (`asset_catalog/catalog_data.js`), the pictures (`asset_catalog/sprites/`) and the sounds (`asset_catalog/sounds/`). The site serves it at **[beta.playants.org/asset_catalog/](https://beta.playants.org/asset_catalog/)**, where it is called "Sprites and sounds".

## What it shows

Three tabs, with a filter box above them that matches an ID or a name (press `/` to reach it, `Esc` to clear it). The page also works on a phone: it scrolls as a document and a table scrolls inside its box.

| Tab | Items | What you get |
|---|---|---|
| **Sounds** (the tab that opens first) | 91 | WAV sound effects (8-bit PCM). Each row has the sample rate, length and size, a play button, an audio player and a "⬇ WAV" download. The page draws no waveform. |
| **Animations** | 1,344 | Animation sequences on a stage, described below. |
| **Sprites** | 2,794 | 8-bit RGBA PNG files (`sprite_0000.png` and on), converted from the archive's paletted bitmaps. A card for each shows the picture, ID, BMP name, size, file name and the animations that use it, with a one-click "⬇ PNG". A click opens an inspector with zoom levels 1x, 2x, 4x and 8x (it opens at 2x) and "⬇ Download PNG". |

The counts are the archive's own (2,794 sprites, 91 sounds, 1,344 animations); the tests of the asset reader pin them.

### The animation stage

- The steps of a sequence are composited from their sprite layers, with the timing of each step and the sounds that a frame triggers (a toggle switches the sound off). The order of the layers is automatic ("Auto (Smart Layering)") or the order of the archive's animation table, reversed or forward ([`TABLE4_ANIMATION_REFERENCE.md`](TABLE4_ANIMATION_REFERENCE.md)).
- Stage background: a checkerboard grid, solid black, neutral gray or game turf green. The picture is centred on its bounding box or on its origin, and a crosshair and the bounds can be shown.
- Zoom: fit to the view, or 25% to 800%. Drag pans, the wheel zooms, a double click fits.
- Playback: play and pause (`Space`), the previous and next frame (arrow keys), a scrubber, and speeds from 0.25x to 2x (1x is 20 frames a second).
- A timeline strip lists the steps, and a table lists the layers of the active step, each with a show / hide box and a PNG download. "⬇ Frame PNG" downloads the whole composite frame.

## Opening it

Online, at the address above. Locally, open the file in a browser: `open asset_catalog/index.html` on macOS, `xdg-open asset_catalog/index.html` on Linux, `start asset_catalog\index.html` on Windows. The data and the files are relative to the page, so it works from a checkout. Its look is the site's stylesheet, `/front/classic.css` (the same Classic look as the front page, with the logo as a link back to it and a **Play** button in the header: see [`PLAY_IN_BROWSER.md`](PLAY_IN_BROWSER.md)); opened as a file it shows plain, without that sheet.

The web image serves it at `/asset_catalog/`; `tests/scripts/test_web_catalog.py` pins what the page must keep (the head's two links, the header, the ids that the script uses, the colour contrast).
