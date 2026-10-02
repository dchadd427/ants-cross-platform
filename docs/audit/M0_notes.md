# M0: the view fingerprint (what it pins, what it cannot see, and where the classic picture is still hard-coded)

Milestone M0 of the widescreen / zoom / touch work: a safety net that changes no production code and no existing test; it ships in v0.0.92 together with the pillarbox pointer fix (what the two have to do with each other is in the section "M0 meets the pillarbox pointer fix" below). The program is `tests/test_app/test_view_fingerprint.cpp` (suite 3.10 of `./run_tests.sh`); its header says how to run it, how to regenerate the golden numbers (`--print`, deliberately, never to silence a failure) and how to look at what changed (`--dump`, `--save`).

## What is pinned

| Family (names) | How | Fingerprints |
|---|---|---|
| Draw calls of `HUD::render` (`hud.*`) | A recording `IRenderer`: every mutating call with all its parameters in final screen coordinates, hashed in order (FNV-1a 64) with the number of calls. The HUD's clock is injected. | 97 |
| Draw calls of the full-screen screens (`screen.*`) | The same recorder on the setup screen (all six maps, hover, pressed, fog, notice), the network room (host, four thumbs, four players), the guest screen, the results screen (2, 3 and 4 teams, an alliance, a quitter, waiting and rows) | 38 |
| The edge scroll (`ptr.edge.*`) | `edge_scroll_step` at EVERY pixel of the 640 x 480 screen: the 60 x 60 map at the default rate from a camera away from the borders and at each of the eight borders and corners (one number each); other rates and map sizes (nine cameras each); a ring of 16 pixels outside the screen | 17 |
| The cursor (`ptr.cursor.*`) | `HUD::evaluate_cursor` at every pixel for 16 selection and dialog states from a camera that sees an own hill, an enemy hill, own ants of every type, an enemy, an ally, enemies on both hills, food, a lunchbox and a bomb, and for two states from all nine cameras. The scenes meet every cursor of the original (checked) | 34 |
| The minimap and the start view (`ptr.minimap.*`, `ptr.start_view.*`, `ptr.class.*`) | The hit zone, the world point under every pixel for five map sizes, the view's step towards it from nine cameras; the start view for every anchor tile; the map view's and the chat log's zones | 12 |
| The camera (`view.camera.*`) | The limits (`clamp_to_bounds`), centring, scrolling, and both conversions, for several map sizes and cameras | 9 |
| Clicks (`ptr.click.*`, `ptr.screen.*`, `ptr.app.*`) | What a press and a release do on a fresh HUD (which control reacts, selection, latched pedestals, commands that the HUD sends, sounds, click markers, scrolling, dialogs) at every fourth pixel, refined: where two neighbouring samples differ the pixels between them are probed too, so every zone's edge is exact to the pixel. 17 HUD states (selections, hills, both buttons, the quit, quick-help, options, start and three alliance dialogs); the setup screen as a local host, a room's host and a guest; the results screen; and the application's own pointer gate for the edge scroll. Every scenario checks that some click did what the scenario says (orders, quits, latches, closes ...) | 26 |
| Pixels (`px.*`) | The real software renderer: `Renderer::render_world` on a 640 x 480 canvas (ants of every type, colour and facing, mirrored clips, effects, score bubbles, fog, selection markers, hit point digits, a crowd, all straddling the borders of the playfield) at the corners and edges of MEDIUM and TINY, the middle and a corner of the other four maps, odd origins; the ten single-frame cursors at every edge of the screen; whole frames of an `Application`: the loading screen (an application with a window on SDL's dummy drivers), the setup screen, the quick help, the match screen at the start view and at the nine camera positions | 74 |
| The self-check | 47 parameters of the recorder one by one, a swap, a dropped call, the count; a masked pixel does not change a pixel hash and any other does; the pointer hashes move with the rate, the camera and a rectangle | 108 checks |

307 fingerprints, 822 checks, about two seconds of CPU time. The numbers are the same on macOS (clang, SDL 2.28) and on Linux (gcc 12, SDL 2.26.5) on arm64 and x86-64: the pixel hashes never contain TrueType text. They were computed at v0.0.88; at v0.0.92, with the pillarbox pointer fix on top, **none of the 307 changed** (macOS with clang, and Debian 12 with gcc 12.2 and `-Werror`).

## What it cannot see (the blind spots)

- **TrueType glyph pixels.** The calls that draw text are fingerprinted (string, position, size, colour), the pixels are not: the pixel hashes mask the boxes where the classic picture has text (the status line, the chat log and its input, the score labels, the setup screen's labels, the dialogs' prompts: fixed rectangles) so that they do not depend on the machine's font library.
- **The frame-rate counter, its sparkline and the version text** (`Application::render_frame`, bottom right, right aligned at x = 632): their width depends on the font and the version changes every release. Masked (a rectangle from x = 470, y = 466). Their geometry is NOT pinned: a milestone that moves them needs its own check.
- **The network overlay** (`Application::render_net_overlay`: "Waiting for ...", "Out of sync", "The host left"; the box is centred in the map view at `17 + (441 - w) / 2`, `y = 26`): it needs a network match and sockets.
- **The four animated cursors** (Move, Target, Attack, Food): `Renderer::render_software_cursor` picks the frame from the wall clock. The ten that have one frame are pinned (and the cursor decision for all fourteen).
- **The pedestal glow** (`HUD::render_pedestal_glow` uses `SDL_GetTicks`; it only shows while the cursor is Move, Food or Target over a pedestal) and the **tile grid overlay** (a debug aid with TrueType text).
- **The audio listener** (`Application` places it at the middle of the view: `PLAYFIELD_W / 2`), sound and music.
- **The pillarbox pointer (v0.0.92)**: `clamp_pointer_event`, `pointer_gone_after` and `Application::button_outside_window` work on SDL events at the top of `Application::handle_events`. The `ptr.app.*` scenarios and the whole-frame pixels call `handle_mouse_motion`, `handle_camera_panning` and `handle_window_event` directly and never feed SDL events through `handle_events`, so no fingerprint sees the clamp (and none moved when it was added). The clamp, the rule for a pointer that has gone and the mouse grab are pinned by `test_pointer_model` (group pillarbox, suite 3.5) and `test_app_integration` 7.8 - 7.8e (suite 3).
- **How SDL scales the 640 x 480 canvas into a window** (fractional scales, bars in a full screen, `SDL_RenderSetLogicalSize`): every hash is of the logical canvas read back at scale 1, from a window of exactly that size. `Renderer::save_screenshot` is not covered either.
- **The engine's own state**: the scenes are made by hand (a `WorldState`, a `Grid`, engine worlds built with the test hooks, ants with named clips); the match frames of `Application` are rendered at tick 0 (the engine has not given its ants a clip yet, so they are not drawn). A change of the simulation cannot move a number, except in the pointer scenes that spawn ants into an engine (positions of spawned ants are tile centres).
- **Dead code**: the fall-backs that fill the screen with plain orange when an animation is missing (`map_select.cpp`, `scorecard.cpp`, `application.cpp`'s loading screen) never run with the shipped archive.

## M0 meets the pillarbox pointer fix (v0.0.92)

The golden numbers were computed at v0.0.88. v0.0.92 puts the pointer fix (`include/ants_app/pointer_clamp.hpp`, `Application::handle_events`, `update_mouse_grab`) on top of them, and the fix changes the event path, so the fingerprints were run on the combined tree to see whether any of them moves:

- **None moved**: 307 fingerprints, 822 checks, 0 failures, on macOS (clang) and on Debian 12 (gcc 12.2, SDL 2.26.5, the whole project built with `-Werror`); the table that `test_view_fingerprint --print` writes is, row for row, the table stored in the program.
- **Why none could move**: the fix acts on SDL events (a position outside the picture becomes the nearest edge pixel; a button released outside the window, or a lifted finger, marks the pointer as outside), and the fingerprints never feed an SDL event through the application's loop. `ptr.edge.*`, `ptr.cursor.*`, `ptr.minimap.*` and `ptr.click.*` ask the model functions and the HUD directly; `ptr.app.edge_gate.*` calls `handle_mouse_motion` with positions up to eight pixels beyond the screen and `handle_camera_panning` (the gate that ignores a pointer outside the picture is still there, behind the clamp, and still pinned: since v0.0.92 an event reaches it already clamped, so only a direct call can still be outside the picture); the whole-frame pixels put the pointer where the scenario wants it with the test hook `note_pointer` and hand window ENTER / LEAVE events to `handle_window_event`. The grab and `os_fullscreen` are not reached by a headless application, and an application with a window (the loading screen) on SDL's dummy driver gets `false` from `os_fullscreen`.
- **What does pin the fix**: `test_pointer_model` group pillarbox (the clamp, the premise through `edge_scroll_step`, after which events the pointer has gone, the grab table) and `test_app_integration` 7.8, 7.8b, 7.8c, 7.8d, 7.8e. The fingerprints and these tests do not overlap: the fingerprints pin what the classic picture and the model functions do, the tests pin the new event path.
- **For M1**: the fix adds one place that hard-codes the classic picture, the two constants of `pointer_clamp.hpp` (listed in the next section, first row). A picture wider than 640 pixels has to give the clamp the size of the screen layout instead of 640 x 480, or the right part of a 16:9 picture would be clamped to the classic one; M1 routes the constants through the screen layout together with the others (the pointer gate of `application.cpp` too).

## Every place that hard-codes 640, 480, 442, 440 or the screen margins (grep of v0.0.88; the `application.cpp` lines renumbered and `pointer_clamp.hpp` added at v0.0.92) and whether a fingerprint covers it

| Place | What | Covered by |
|---|---|---|
| `renderer.hpp` `CANVAS_WIDTH` / `CANVAS_HEIGHT` (640 x 480); `renderer.cpp:336` (`init`) and `:436` (`set_fullscreen`) `SDL_RenderSetLogicalSize` | the logical size | every pixel hash (the canvas is read back at scale 1; a changed logical size changes the picture) |
| `renderer.cpp:1964` `save_screenshot` | the 640 x 480 fall-back size | not covered |
| `renderer.hpp` `PLAYFIELD_X / Y / W / H` (16, 21, 442, 440) and `ViewportCamera::viewport_w / h` | the map view | `px.world.*`, `px.app.match.*` (clip, origin), `view.camera.*` (limits, centring, both conversions) |
| `renderer.cpp:138 - 150` `world_to_screen` / `screen_to_world` | the camera's conversions | `view.camera.*` |
| `renderer.cpp:634`, `:755` the playfield's clip (`render_world`, `render_map_layers`) | the clip rectangle | `px.world.*` (sprites straddle every border) |
| `renderer.cpp:742 - 768`, `:864 - 868`, `:948`, `:1013 - 1016`, `:1595 - 1598` the cull and tile ranges (`PLAYFIELD_W + 31`, `+ kMargin` ...) | which tiles and sprites are drawn | `px.world.*` at the corners and edges and at odd origins (the margins beyond the 80 pixels that the ants straddle are never observable) |
| `renderer.cpp:998 - 999`, `:1105 - 1108`, `:1178 - 1179`, `:1296 - 1297`, `:1485 - 1488`, `:1530`, `:1573 - 1574` the origin and cull margins (160 / 320 / 128) of ants, effects, selection markers, hill brackets, the click marker, score bubbles | where things are drawn | `px.world.*` (ants, effects, bubbles, markers, hill brackets, click marker, fog) |
| `renderer.cpp:1405 - 1406` the hit point digits at `PLAYFIELD + a.px - camera.x` | Ctrl+L digits | `px.world.MEDIUM.selected_hp` |
| `renderer.cpp:1603 - 1611`, `:1645 - 1661` the tile grid overlay (mouse tile, the badge at `PLAYFIELD_X + 4`, `PLAYFIELD_Y + PLAYFIELD_H - 18`, the food badges) | a debug overlay with TrueType text | NOT covered |
| `renderer.cpp` the part order, the fog autotiles, the sorting | | `px.world.*` |
| `hud.hpp` `PLAYFIELD_X / Y`, `MAP_LEFT / TOP / RIGHT / BOTTOM` (16, 21, 458, 461), `in_map_rect`, `in_minimap_rect` (480, 35, 599, 126), `kChatView*` (482, 299, 138, 101) | the HUD's own copy of the zones | `ptr.class.*`, `ptr.cursor.*`, `ptr.click.*`, `hud.chat.*` (the clip) |
| `hud.cpp:374` the backing fill (480, 22, 160, 458); the `uishell` at (0, 0); `:602` the minimap at (480, 35) 119 x 91; `:715 - 723` the view frame (`PLAYFIELD_W / H` scaled) | the shell | `hud.*` (every call), `px.app.match.*` |
| `hud.cpp` the status line (481, 254), the chat input (481, 424), the score slots (`kBottom`, `kLocal`), the dialogs' origin (100, 100) and their labels, the quick help | the panel and the dialogs | `hud.*` |
| `hud_input.cpp:17` `kSlot` (the three pedestal zones, 628 as the right edge of Stop); `:100 - 101` the rubber band's clamp (`MAP_* + 1`); `:155 - 156`, `:338 - 339`, `:418 - 419`, `:442 - 450` the screen to world conversions | the pointer | `ptr.click.*` (zones to the pixel), `ptr.cursor.*`, `hud.marquee.*` |
| `edge_scroll.hpp` `kOrigViewW / H` (442 / 440), `kScreenW / H` (640 / 480), `kBand`, `kInner`, the pre-test (13 .. 627, 13 .. 467), `scroll_to_show`, the minimap's `119` / `91` scale and `221` / `220`, `start_view_origin` (160 / 192) | the edge scroll, the minimap, the start view | `ptr.edge.*`, `ptr.minimap.*`, `ptr.start_view.*` |
| `pointer_clamp.hpp:18 - 22` (v0.0.92) `kScreenWidth` / `kScreenHeight` (640 / 480) and `clamp_to_screen_x` / `_y` (0 .. 639, 0 .. 479), used by `clamp_pointer_event` at the top of `Application::handle_events` | a pointer beyond the picture (over a black bar of a wide window) is the pointer on the nearest edge pixel | NOT covered by the fingerprints (they never go through `handle_events`); pinned by `test_pointer_model` group pillarbox and `test_app_integration` 7.8 - 7.8e |
| `application.cpp:1226` the pointer gate (`>= 640`, `>= 480`) | when the input task runs (an event reaches it already clamped since v0.0.92) | `ptr.app.edge_gate.*` (direct calls) |
| `application.cpp:1327 - 1328` the audio listener (`PLAYFIELD_W / 2`) | sound | NOT covered |
| `application.cpp:1558 - 1561` the network overlay (`17 + (441 - w) / 2`, `y = 26`, a box of `w + 12`) | the overlay | NOT covered (needs a network match) |
| `application.cpp:1592 - 1638` the frame-rate plate (right edge 632, `FPS_OVERLAY_*` rows 467 - 480, the sparkline) and `fps_overlay.hpp` | remake overlay | NOT covered (masked) |
| `application.cpp:1736 - 1762` the loading screen (a 640 x 480 fill, the logo at (25, 23), credits (32, 299), strip (40, 315), the bar (229, 448) 234 x 8) | the loading screen | `px.app.loading.*` |
| `application.cpp:1766 - 1771` the quick help screen (`qh_screen`, the START! button) | the quick help | `px.app.quickhelp.*` |
| `map_select.cpp:235`, `scorecard.cpp:124` the 640 x 480 fall-back fill | dead code with the shipped archive | not reachable |
| `map_select.hpp` `BTN_*`, `LABEL_X`, `NAME_Y`, ...; `scorecard.hpp` `FIRST_ROW_Y`, `COLUMN_X`, `QUIT_BTN_*`; `options_screen.hpp` `SLIDER_*`, `EDIT_*`, `RETURN_*` | the screens' zones and labels | `screen.*`, `hud.options.*`, `ptr.screen.*`, `ptr.click.dialog.*` |
| `text_layout.cpp:82` `caret_x < CANVAS_WIDTH` | the caret of an edit field | `hud.chat.typing.*`, `hud.options.*` |
| `window_layout.hpp` `kMinWindowWidth` (320) and the grid maths | the window, not the picture | `test_input_model` (3.4), not this suite |

## How the numbers were checked

- **Deterministic.** Two runs of `--print` give the same table byte for byte, and the table is the same on Linux (Debian 12, gcc 12.2, SDL 2.26.5, SDL_ttf with its own FreeType) as on macOS (clang, SDL 2.28) on arm64 and on x86-64 (the Linux build run under emulation; its first run found one scene, `hud.antheavy`, that drew two random numbers as arguments of one call, whose order of evaluation is the compiler's: they are drawn in separate statements now): the hashes of the draw calls, the pointer sweeps and the pixels (software renderer, alpha blits included) are equal everywhere. The program compiles without a warning under `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow` with clang (`ANTS_WERROR=ON`) and with gcc 12. (At v0.0.88 the application library did not build under gcc 12 with `-Werror`: `LevelData::DimensionProp` gave `-Wconversion` errors in `application.cpp`, `map_select.cpp` and many tests; M0 changed no production code and left them. v0.0.92 fixes them, see its CHANGELOG entry: the whole project, tests included, builds with gcc 12 and `-Werror`, and these numbers are the same there.) A different seed of the engine moves none of the `px.app.match.*` numbers.
- **Sensitive.** The program's own self-check (above) and 34 deliberate mutations of the production code, each made in a scratch copy, built there and run (the copy's production tree was identical to the worktree's):

| # | Mutation | Fingerprints that fail |
|---|---|---|
| 1 | a sprite one pixel off (the clock's colon at x = 91, `hud.cpp`) | 110 (all `hud.*`, the match frames) |
| 2 | a text colour (the status line's (79, 0, 143) becomes (79, 0, 144)) | 89 `hud.*` (the scenes that show a status text) |
| 3a | a clip rectangle one pixel taller (the chat log's) | 96 `hud.*` |
| 3b | a clip rectangle one pixel wider (the playfield's, `Renderer::render_world`) | 38 `px.world.*` |
| 4a | an edge-scroll band one pixel wider (north band 13 rows) | 47 `ptr.edge.*`, `ptr.cursor.*`, `ptr.click.*`, `ptr.app.*` |
| 4b | an edge-scroll inner strip one pixel wider (east strip from 634) | 24 |
| 5 | two draw calls swapped (the status line after the chat log) | 89 `hud.*` |
| 6 | one draw call dropped (the clock's colon) | 110 |
| 7 | the minimap's origin one pixel right (`rx = 481`) | 110 |
| 8 | `PLAYFIELD_X` 17 (the renderer's map view origin) | 58 (`px.world.*`, `px.app.match.*`, `view.camera.*`) |
| 9 | the move pedestal's click zone one pixel wider | 6 `ptr.click.*` |
| 10 | the application's pointer gate ignores the last row | 3 `ptr.app.edge_gate.*` |
| 11 | the loading bar one pixel right | 3 `px.app.loading.*` |
| 12 | the start view one pixel further right | 3 (`ptr.start_view.*`, `px.app.match.*`) |
| 13 | a score label 12 px high instead of 14 | 96 `hud.*` |
| 14 | the minimap's view frame one pixel wider | 109 |
| 15 | `ViewportCamera::world_to_screen` one pixel lower | 55 |
| 16 | the results screen's rows one pixel right (`NAME_X`) | 8 `screen.results.*` |
| 17 | the HUD's backing fill one pixel shorter | 96 `hud.*` |
| 18 | an enemy ant shows the select cursor (the attack cursor never appears) | 16 `ptr.cursor.*` |
| 19 | the results screen's Leave zone one pixel right | 1 `ptr.screen.results.rows` |
| 20 | the minimap's hit zone one pixel wider (`in_minimap_rect`) | 11 |
| 21 | the map view's right edge 459 (`MAP_RIGHT`) | 31 |
| 22 | the Stop button's zone one pixel lower | 4 |
| 23 | the rubber band kept two pixels inside the view | 1 `hud.marquee.clamped_low` |
| 24 | the setup screen's START zone one pixel right | 2 `ptr.screen.setup.*` |
| 25 | a hit point digit one pixel left | 2 `px.world.MEDIUM.selected_hp`, `px.app.*` |
| 26 | the quit dialog's Yes zone one pixel wider | 1 `ptr.click.dialog.quit` |
| 27 | the loading screen's strip one pixel lower | 4 `px.app.loading.*` |
| 28 | `ViewportCamera::clamp_to_bounds` allows the view one pixel beyond the map | 3 `view.camera.*` |
| 29 | the quick help's START! shows the pressed picture when hovered | 1 `px.app.quickhelp.hover_start` |
| 30 | `minimap_point`'s scale is 120 instead of 119 across | 18 (`ptr.minimap.*`, `ptr.cursor.*`, `ptr.click.*`) |
| 31 | the setup screen's Up button zone one pixel lower | 2 `ptr.screen.setup.*` |
| 32 | a colour of the chat log (the green header) | 8 |
| 33 | the options screen's Return button zone moves | 1 `ptr.click.dialog.options` |
| 34 | the allied score box is split one pixel earlier | 9 |

  What the experiment found, and what changed in the program because of it: (a) mutation 9 was NOT noticed by the first click sweep (every second pixel: the zone's changed edge lay on an odd column), so the click sweeps became refined sweeps (every fourth pixel, and wherever two neighbouring samples differ the pixels between them as well: every zone is exact to the pixel) and mutation 9 is noticed; (b) mutation 23 was not noticed either (the scene dragged the rubber band only past the far edges of the view: only the upper clamp was exercised), so `hud.marquee.clamped_low` drags past the near corner; (c) two variants were equivalent and are NOT counted: a pointer gate that admits x = 640 (no edge band contains x = 640, so nothing changes) and the plain orange fall-back fill of the setup screen (dead code with the shipped archive).
