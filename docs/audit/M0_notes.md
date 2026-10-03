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

## M1: the screen layout (what it replaced, what is left for M3 and M4)

Milestone M1 of the widescreen work: `include/ants_app/screen_layout.hpp` (pure, no SDL) says, as numbers, where everything of the classic picture is, and the places of the table above that hard-coded the original's 640 x 480 screen read it. **No behaviour changes**: with `ScreenLayout::classic()` (the default of the renderer, the HUD and the application) the 307 fingerprints above are unchanged and so is every other suite; the only edits to existing tests are the two calls of `Renderer::init` that lose their dead third argument (CHANGELOG, "Rewritten tests"). The new suite 3.13 (`tests/test_app/test_screen_layout.cpp`) pins the model and the wiring.

### The model

`ScreenLayout` has a width and a height (640 x 480 = `classic()`, the original's numbers; `with_size(W, H)` for a bigger canvas, never smaller than the original's screen) and the corner of the map view (the original's (16, 21); only a test moves it, to see that every consumer reads it). From them: `dx() = W - 640` (what is anchored to the right edge moves right by it), `dy() = H - 480` (what is anchored to the bottom edge moves down), `right(x)` / `bottom(y)` (a classic coordinate that is anchored), and the shared rectangles `view()` (16, 21, 442 + dx, 440 + dy), `minimap()` (480 + dx, 35, 119, 91; its hit zone too), `chat_view()` (482 + dx, 299, 138, 101 + dy), `panel_fill()` (480 + dx, 22, 160, 458 + dy) and `score_slot(k)` (slot 0 the local team's, in the top bar, right anchored; slots 1 .. 3 the other teams', in the bottom strip, right and bottom anchored, as the corrected mock-up of section 52.P of the plan has them next to the panel; a slot past the third repeats it, so that more than four players are a change of this one function). `CornerPlate::for_canvas(W, H)` (`fps_overlay.hpp`) is the corner of the frame-rate plate and the version: the corner of the CANVAS (rule 7), not of the picture. The classic numbers are written out independently in the test (the view, the minimap, the four score slots, the eight edge strips of the original, the zones at every pixel against `HUD::in_map_rect` / `in_minimap_rect`).

### What M1 replaced

| Place (the table above) | Now |
|---|---|
| `renderer.hpp` `CANVAS_*`, `PLAYFIELD_*`, `ViewportCamera::viewport_w / h` | the constants stay as the CLASSIC numbers (tests and defaults); `ViewportCamera` has the view's corner (`view_x`, `view_y`) next to its size and `set_view(rect)`; the renderer has `set_layout`, `set_canvas_size` and `refit_canvas` |
| `renderer.cpp` `world_to_screen` / `screen_to_world` | the camera's own corner and size |
| the playfield's clip (`render_world`, `render_map_layers`), the cull and tile ranges (layer 1, the layer-2 pass with its margin of three tiles, the fog, the tile grid), the origin and cull margins of ants, effects, selection markers, hill brackets, the click marker, score bubbles, the hit point digits | `layout_.view()` |
| `renderer.cpp` `init` and `set_fullscreen` (`SDL_RenderSetLogicalSize`) | `set_canvas_size` (SDL's logical size) at the start and `refit_canvas` on every size event of the window; `set_fullscreen`, `integer_scale` and `ApplicationConfig::integer_scaling` are deleted (see below) |
| `renderer.cpp` `save_screenshot` (the 640 x 480 fall-back size) | the renderer's canvas (the rewrite for a canvas of another shape is M2) |
| `hud.hpp` `in_map_rect`, `in_minimap_rect`, `kChatView*`, `PLAYFIELD_*`, `MAP_*` | the static ones stay as the classic numbers; the HUD asks `over_map`, `over_minimap`, `in_chat_view` (its layout) |
| `hud.cpp` the buttons' rectangles (Help, Options, Quit, the three pedestal slots, the hatch and ally pedestals, [All], [Team]), the backing fill, the minimap and its view frame, the status line (481, 254), the chat input (481, 424), the score slots | `HUD::apply_layout`, ONE function that `init()` (so every new match) and `set_layout()` both run: a layout that was set earlier is not lost when the next match resets the HUD; the pedestal slots of `hud_input.cpp` (`kSlot`) are the three pedestal buttons' rectangles now |
| `hud.cpp` the chat log's scroll arithmetic (`kChatViewH`) | the chat view's height of the layout |
| `hud_input.cpp` the rubber band's clamp, the cursor's strips, the screen to world conversions, the minimap's right click | the layout's view, corner and minimap |
| `edge_scroll.hpp` `kOrigViewW / H`, `kScreenW / H`, the strips, the pre-test, `scroll_to_show`, the minimap's 119 / 91 / 221 / 220, `start_view_origin` | overloads with a `ScreenLayout` (the old signatures stay and are the classic layout's) |
| `pointer_clamp.hpp` `kScreenWidth / Height` | `clamp_pointer_event(e, layout)` and `(e, picture_rect)` (the picture's corner is subtracted first: for a picture inside a bigger canvas) |
| `application.cpp` the pointer gate (>= 640, >= 480), the audio listener, the network overlay's box (`net_overlay_box`), the first pointer position, the start view, the loading screen's fill | the application's layout (`Application::set_layout` gives it to the renderer and the HUD) |
| `application.cpp` the frame-rate plate (right edge 632, rows 467 - 479) and `latency_corner` | `CornerPlate` (the corner of the canvas); the latency layout has overloads that take it |
| `text_layout.cpp:82` `caret_x < CANVAS_WIDTH` | `draw_edit_line(..., screen_w)`: the HUD passes its picture's width, the pages the original's 640 |
| `map_select.cpp:235`, `scorecard.cpp:124` (dead fall-backs) | named constants |

### What M1 removed

`Renderer::set_fullscreen`, `Renderer::init`'s `integer_scale` argument and member, `ApplicationConfig::integer_scaling`: the integer scale was never enabled (the config field was false and nothing set it), so the game has always scaled by SDL's logical size alone: the largest scale that fits, centred, bars where the shape differs, a fractional scale unless the window is a multiple of 640 x 480, nearest-neighbour filtering (1920 x 1080 shows the picture at 2.25x, 1280 x 960 at 2x). The README's lines that spoke of "integer scaling" and a "pixel-crisp 4:3 display" were wrong and say this now. What `set_fullscreen` did besides (it applied SDL's logical size again on every size event) is kept as `refit_canvas`, which the window's size events call.

### What is left (for M3 and M4)

- **Absolute coordinates inside the original's animations.** The shell (`uishell`, 14 parts), the pedestal art, the egg tray, the Stop button, the lunchbox indicator, the send buttons' art, the chat cover, the top bar's buttons' hover art and the quick help are CHD animations whose parts carry their own absolute screen coordinates (`draw_animation_frame0` takes an offset; the HUD passes (0, 0)). The rectangles that M1 placed move with a layout, their art does not yet: M3 draws these parts by their anchor (the offset argument, or an origin of the renderer).
- **Dialogs and pages.** The quit and alliance dialogs, the "get ready" modal, the options screen, the quick help, the loading, setup and results screens are pictures of the original's screen with their own coordinates (origin (100, 100), the screens' zones); M1 leaves them, M2 centres them in a bigger canvas, M3 decides what a wide match screen does with them (`to_modal` for the input).
- **The pedestal's own state machine** (`pedestal.cpp`) draws at absolute coordinates.
- **`window_layout.hpp`** (the 4:3 cells of the grid and the minimum window) is M2's.
- The view's CORNER is the same (16, 21) in every layout of `with_size`: the ~40 reads of `view().x / y` cannot be told from the old constants by such a layout, which is why the layout carries the corner as a field that a test can move (mutations M1-02, M1-46 - M1-48, M1-61, M1-62, M1-66, M1-67).

### Proof

- The 307 fingerprints are unchanged (macOS with clang, `-Werror`; the Linux build is in the CHANGELOG entry), the other existing suites pass without a change.
- Suite 3.13 `test_screen_layout`: the model against the original's numbers, the HUD's rectangles with the classic layout one by one and with `with_size(960, 540)` (exactly the right-anchored ones moved by dx = 320, the bottom-anchored by dy = 60, nothing else; the dialog's buttons stay), a layout that survives `init`, `reset` and a new match, the zones and presses, the drawing of the backing fill, the minimap with its view frame, the status line, the chat input and caret, the four score boxes and the rubber band, the eight strips of the scroll and the minimap drag and the start view and Ctrl+N and the cursor, the pointer's limits, the plate's corner, the camera, the renderer (the clip, the culling and the origin) and the application (listener, pointer gate and limits, start view, the plate in the canvas's corner, a size event). **The renderer is also checked against itself**: whatever the classic layout draws for a window of the world (terrain, objects, 300 ants with their ears and hit point digits, effects, score bubbles, the click marker, the hill's brackets, fog of war) is, pixel for pixel, a part of what the 960 x 540 layout draws for the larger window, at five offsets, and a view whose corner is (40, 30) draws the picture of the view at (16, 21), moved.
- **Mutations** (one change at a time in a scratch copy, the suite rebuilt and run, the file restored byte for byte): 76; 75 are killed, one is equivalent. The first run killed 72 and left four: M1-42 (Ctrl+N scrolled by the classic view's size), M1-43 (the chat log's limit used the classic height), M1-48 (the rubber band's corner) and M1-10; the first three got their tests (the Ctrl+N, chat and drag checks above) and are killed now. M1-10, the pre-test of the edge scroll (`mx < sw - 13` taken as 640 - 13), is equivalent: the eight bands are 12 px thick and the quiet area starts 13 px in, so no pixel of a band is ever in the quiet area and the test never changes an answer.

| # | Mutation | Killed by |
|---|---|---|
| M1-01 | `screen_layout.hpp`: view() does not grow by dx / dy | 46 checks |
| M1-02 | `screen_layout.hpp`: view() ignores the corner field | 5 checks |
| M1-03 | `screen_layout.hpp`: minimap() not right anchored | 15 checks |
| M1-04 | `screen_layout.hpp`: chat_view() does not take the extra height | 2 checks |
| M1-05 | `screen_layout.hpp`: panel_fill() does not take the extra height | 8 checks |
| M1-06 | `screen_layout.hpp`: bottom score slots not bottom anchored | 8 checks |
| M1-07 | `screen_layout.hpp`: the local score slot not right anchored | 1 checks |
| M1-08 | `edge_scroll.hpp`: east strip at x = 640 | 10 checks |
| M1-09 | `edge_scroll.hpp`: the largest x origin from the classic view | 1 checks |
| M1-10 | `edge_scroll.hpp`: the quiet area ends 13 px before 640 | equivalent (see below) |
| M1-11 | `edge_scroll.hpp`: the target point scaled by the classic view / screen | 3 checks |
| M1-12 | `edge_scroll.hpp`: the minimap point from (480, 35) | 7 checks |
| M1-13 | `edge_scroll.hpp`: the minimap drag square is 221 wide | 2 checks |
| M1-14 | `edge_scroll.hpp`: the start view scrolls by the classic view | 3 checks |
| M1-15 | `pointer_clamp.hpp`: the picture clamp holds x to 639 | 7 checks |
| M1-16 | `pointer_clamp.hpp`: the picture clamp does not subtract the picture corner | 3 checks |
| M1-17 | `pointer_clamp.hpp`: the layout clamp is the classic rectangle | 13 checks |
| M1-18 | `fps_overlay.hpp`: the plate stands 632 from the left whatever the canvas | 7 checks |
| M1-19 | `fps_overlay.hpp`: the plate stands at row 468 whatever the canvas | 6 checks |
| M1-20 | `latency_corner.cpp`: stacked latency texts above the classic plate | 1 checks |
| M1-21 | `latency_corner.cpp`: stacked latency texts end at the classic corner | 1 checks |
| M1-22 | `net_overlay.cpp`: the overlay centred in the classic view | 1 checks |
| M1-23 | `hud.cpp`: the Help button not right anchored | 2 checks |
| M1-24 | `hud.cpp`: [All] not bottom anchored | 2 checks |
| M1-25 | `hud.cpp`: Stop not right anchored | 2 checks |
| M1-26 | `hud.cpp`: the hatch pedestal not right anchored | 1 checks |
| M1-27 | `hud.cpp`: the quit dialog Yes button moves with the layout | 1 checks |
| M1-28 | `hud.cpp`: init() places the classic layout, losing the one set earlier | 5 checks |
| M1-29 | `hud.cpp`: set_layout() does not place the rectangles | 20 checks |
| M1-30 | `hud.cpp`: the minimap is drawn at (480, 35) | 2 checks |
| M1-31 | `hud.cpp`: the status line at x = 481 | 1 checks |
| M1-32 | `hud.cpp`: the chat input at (481, 424) | 2 checks |
| M1-33 | `hud.cpp`: the chat input caret limit is 640 | 1 checks |
| M1-34 | `hud.cpp`: the local score slot is the classic one | 1 checks |
| M1-35 | `hud.cpp`: the other teams use the classic slots | 3 checks |
| M1-36 | `hud.cpp`: the minimap view frame is the classic view scaled | 1 checks |
| M1-37 | `hud.cpp`: the backing fill is the classic one | 1 checks |
| M1-38 | `hud.cpp`: the press on the minimap tests the classic zone | 1 checks |
| M1-39 | `hud.cpp`: the left press on the map tests the classic zone | 1 checks |
| M1-40 | `hud.cpp`: the input task scrolls by the classic strips | 2 checks |
| M1-41 | `hud.cpp`: the minimap drag follows the classic minimap | 1 checks |
| M1-42 | `hud.cpp`: Ctrl+N scrolls to show the ant by the classic view size | 1 checks |
| M1-43 | `hud.cpp`: the chat view height is the classic one in the scroll limit | 2 checks |
| M1-44 | `hud_input.cpp`: the cursor ignores the layout for the scroll strips | 3 checks |
| M1-45 | `hud_input.cpp`: the cursor tests the classic map rectangle | 4 checks |
| M1-46 | `hud_input.cpp`: the cursor takes the screen point to the world by (16, 21) | 1 checks |
| M1-47 | `hud_input.cpp`: a click takes the point to the world by (16, 21) | 1 checks |
| M1-48 | `hud_input.cpp`: the rubber band selects with the classic corner | 1 checks |
| M1-49 | `hud_input.cpp`: the rubber band is held inside the classic view | 1 checks |
| M1-50 | `hud_input.cpp`: a right click on the minimap uses the classic minimap | 1 checks |
| M1-51 | `hud_input.cpp`: a release on the map without a band tests the classic zone | 1 checks |
| M1-52 | `renderer.cpp`: render_world clips to the classic view | 7 checks |
| M1-53 | `renderer.cpp`: render_map_layers clips to the classic view | 4 checks |
| M1-54 | `renderer.cpp`: layer 1 tiles up to the classic view | 9 checks |
| M1-55 | `renderer.cpp`: layer 2 anchors up to the classic view | 3 checks |
| M1-56 | `renderer.cpp`: template sprites culled by the classic view | 4 checks |
| M1-57 | `renderer.cpp`: ants culled by the classic view | 3 checks |
| M1-58 | `renderer.cpp`: effects culled by the classic view | 1 checks |
| M1-59 | `renderer.cpp`: the hill brackets culled by the classic view | 1 checks |
| M1-60 | `renderer.cpp`: the fog covers up to the classic view | 4 checks |
| M1-61 | `renderer.cpp`: the hit point digits at the classic corner | 1 checks |
| M1-62 | `renderer.cpp`: the click marker at the classic corner | 1 checks |
| M1-63 | `renderer.cpp`: set_layout does not give the camera the view | 5 checks |
| M1-64 | `renderer.cpp`: set_canvas_size does not reach SDL | 8 checks |
| M1-65 | `renderer.cpp`: refit_canvas does nothing | 18 checks |
| M1-66 | `renderer.cpp`: the camera does not take the view origin | 2 checks |
| M1-67 | `renderer.hpp`: ViewportCamera::set_view ignores the origin | 2 checks |
| M1-68 | `text_layout.cpp`: the caret limit is the classic 640 | 2 checks |
| M1-69 | `application.cpp`: the pointer gate is 640 x 480 | 1 checks |
| M1-70 | `application.cpp`: the sound listener is the centre of 442 x 440 | 1 checks |
| M1-71 | `application.cpp`: the plate is in the classic corner | 2 checks |
| M1-72 | `application.cpp`: the start view by the classic view size | 1 checks |
| M1-73 | `application.cpp`: the pointer clamp is the classic screen | 4 checks |
| M1-74 | `application.cpp`: a size event does not apply the canvas again | 2 checks |
| M1-75 | `application.cpp`: Application::set_layout does not reach the renderer | 2 checks |
| M1-76 | `application.cpp`: Application::set_layout does not reach the HUD | 3 checks |

## M2: the 16:9 canvas behind `--aspect` (what it does, what is left for M3)

Milestone M2 of the widescreen work. **The default is still the classic picture**: with no option and no settings key the game is what it was (the 307 fingerprints, every other suite and the new suite 3.13 pass with M2 in). The owner's decision for M2 (2026-10-02): the 16:9 picture is a FIXED logical canvas of **960 x 540**; everybody who asks for it sees exactly the same world area (no caps in online rooms), SDL scales the canvas into the window, the shape of the monitor is a later option. The new suite 3.14 is `tests/test_app/test_canvas_layout.cpp`.

### What it does

- **The canvas.** `include/ants_app/canvas_layout.hpp` (pure): `Aspect` (`Classic4x3` = 640 x 480, `Wide16x9` = 960 x 540), `CanvasLayout` (any size; `centred(w, h)` puts a picture in the middle, the classic picture in the wide canvas at (160, 30)) and `CanvasLayout::fit(window w, h)`: SDL's own arithmetic for a logical size (the largest scale that fits, centred, bars where the shapes differ; single precision floats and floor, as `UpdateLogicalSize` does), so that the program can say where the picture is without asking SDL. The table the tests pin: 1920 x 1080 shows 960 x 540 at 2x exactly, 3840 x 2160 at 4x, 2560 x 1440 at 2.667x, 1280 x 720 at 1.333x, 1366 x 768 at 1.422x (one column of bar), 1440 x 900 at 1.5x with bars of 45 rows, **2880 x 1800 at 3x with bars of 90 rows above and below (2880 x 1620)**, 2560 x 1080 at 2x with bars of 320 columns; the classic 640 x 480 canvas in 800 x 600 at 1.25x, in 1280 x 960 at 2x, in 1920 x 1080 at 2.25x with bars of 240 columns (uneven pixels: this is how the game has always been scaled), in 2560 x 1440 at 3x. The scale is a whole number exactly when the limiting side of the window is a multiple of the canvas's. The model is checked against a few thousand window sizes by rule and against SDL itself (the corners of the canvas in 32 real windows).
- **The option.** `--aspect 16:9` or `--aspect 4:3`, and the settings key `aspect` (read at the start; the command line wins; a value that is neither is reported and ignored, so a settings file never stops the game). Anything else on the command line is refused with `only 16:9 and 4:3 for now` and the game does not start (`startup_error`, like a bad `--bot`). 4:3 is the original's canvas and the default. The web build stays 4:3 whatever it is given (M5 flips it with its page).
- **SDL's logical size** is the canvas (`Renderer::set_canvas_size`; the window's size events apply it again, `refit_canvas`), not a size that follows the window: everybody with the same aspect has the same picture. Fullscreen is the same canvas as large as the monitor allows (bars on a 16:10 or 21:9 monitor).
- **The window.** A window of the 16:9 aspect opens at the **largest scale in steps of 0.5 of 960 x 540 that fits the usable part of the display** (title bar and frame counted, at least 1x, centred: `default_canvas_window`; M2 had the largest whole-number multiple, which left a typical laptop at 1x: the review of M3 and the owner's decision, see "Review fixes of the widescreen work" below); `--window-size` and `--grid` still win, and the grid's cells are 16:9 then (`grid_cell_window` takes the canvas's shape; the default is 4:3).
- **The picture inside the canvas.** In M2 the match screen and the original's pages are still the 640 x 480 picture: the application draws it centred (`picture_` = (160, 30, 640, 480) in the 16:9 canvas, the whole canvas in 4:3). `Renderer::set_picture(rect)` is the hook: every position the renderer hands to SDL has the picture's corner added (`placed()`: sprites, parts of the world, text, the digits, the RGBA image, fills, frames, clips) and `clear_clip_rect` / a new frame restore the PICTURE as the clip, so nothing is drawn beyond it and the bars stay black. The pointer: the canvas position becomes the picture's own (`clamp_pointer_event(e, picture_rect)` subtracts the corner, then holds it to the picture), so **a pointer over a bar is the picture's nearest edge pixel and the map scrolls** (the pillarbox rule of v0.0.92, for a canvas that has bars of its own). The frame-rate plate and the version are the CANVAS's, not the picture's (rule 7): `render_frame` sets the picture to the whole canvas for them and back for the cursor.
- **Alt+Enter** (native builds) toggles fullscreen (`Application::toggle_fullscreen`, SDL's desktop fullscreen, as `--fullscreen`). The key belongs to the window: no screen sees it (Enter alone is still the setup screen's START and the chat's send), a held key toggles once; a macOS fullscreen Space is left with the operating system's controls; the web has its own button.
- **`Renderer::save_screenshot`** writes the picture as the window shows it (the canvas scaled into the window, without the bars: `CanvasLayout::fit` gives the rectangle). It used to write a file of the window's size with the picture at its top left corner, which is right only when the window has the canvas's shape: a 16:9 canvas in a 1280 x 960 window gave 1280 x 960 with the bottom quarter empty. The headless `--screenshot` of a 4:3 window of the canvas's shape is the same file as before.

### A known limit: seams at fractional scales (found while taking the screenshots)

At a scale that is not a whole number SDL's SOFTWARE renderer (the one a headless run and the screenshot tool use) rounds the destination rectangle of every sprite to whole pixels, and two neighbouring tiles can leave a one-pixel gap between them: black seams in the terrain and in the pages' frames (a grid at 1.333x, a seam every few tiles at 2.667x; none at 2x or 4x). It is not new: the classic 4:3 picture does the same at any fractional scale (an 800 x 600 window, 1.25x, shows the seams in the setup screen: `docs/AUDIT_ONE_TO_ONE.md` already says "0 seams" holds for integer scales only). What M2 changes is how often a player meets it: the 16:9 canvas is a whole-number multiple on 1080p and 4K screens but fractional on 720p, 1366 x 768, 1600 x 900 and 1440p. Whether an accelerated renderer (Metal, OpenGL, WebGL) keeps the edges of adjacent quads watertight (it positions them in floating point) is to be checked on a real window; the remedies, if it does not, are in section 52.P of the plan: the picture drawn once into an offscreen target and that one texture scaled (seamless, uneven pixels as now, at the price of the text's sharpness unless the text is drawn afterwards at the window's resolution), or a whole-number-only scale with larger bars. The default stays the classic picture until this is decided.

### What is left (for M3)

M3 makes the match screen the wide picture itself: `ScreenLayout::with_size(960, 540)` (dx = 320, dy = 60) is the geometry (M1), the application gives it to `Application::set_layout`, and then the match picture is the whole canvas while the pages and the dialogs stay centred 640 x 480 pictures: `picture_` must become two rectangles (the match's and the pages'), the pointer clamp takes the one of the screen that is up, and the HUD draws its dialogs through a corner (an origin hook of the renderer, `Renderer::set_picture` is the concrete one and `IRenderer` has none yet). The HUD's art that has absolute coordinates inside the original's animations (listed in the M1 section) is M3's too. What M2 leaves in place for it: the layout is a value everywhere, the plate and the pointer already work on a canvas that is bigger than the picture.

### Proof

- The classic path: the 307 fingerprints and the suites 3 - 3.13 unchanged (the fingerprints of the classic picture, the M1 suite with the application's picture rectangle in the loop).
- Suite 3.14 `test_canvas_layout` (the table above, the rules, SDL's own layout, the centred picture, the windows that open and the grid, the parsing and the refusals, the option and the key and their precedence, the application's canvas and picture, **the 16:9 application's match screen and setup screen are, pixel for pixel, the 4:3 application's frame (the plate masked) centred in the canvas**, the bars black, the plate in the canvas's corner and not in the picture's, the renderer's picture (origin, clip, text from the cache, the digits, an RGBA image), the pointer over every bar and far beyond the window, Alt+Enter, the screenshot of seven canvas / window pairs).
- **Mutations** (one change at a time in a scratch copy, the suites 3.13 and 3.14 rebuilt and run, the file restored byte for byte): 55, all killed. The first run killed 50 and left five (M2-05 a letterboxed height that rounds up: no table case had a fraction, M2-24 `clear_clip_rect` that clears the clip altogether: the test filled exactly the picture, M2-46 a repeated Alt+Enter: two repeats toggle back, M2-51 the grid's cells ignoring the aspect: only the pure function was tested, M2-52 the frame not setting the picture: it is the previous frame's already, a frame must not depend on that); each got a test and is killed now. Not covered, and why: the 5 x 7 bitmap font's own placement (`SDL_RenderDrawPoint` of the fall-back used only when no TrueType font is found: the repository ships one), the debug tile grid and the dead brackets / synthetic terrain fall-backs of the renderer (they use `placed()` too and are not reachable in a game), and the web build's forced 4:3 (the Docker build compiles it).

| # | Mutation | Killed by |
|---|---|---|
| M2-01 | `canvas_layout.hpp`: fit: the tolerance of equal shapes is 0.01 | 7 checks |
| M2-02 | `canvas_layout.hpp`: fit: a canvas wider than the window is scaled by the heights | 24 checks |
| M2-03 | `canvas_layout.hpp`: fit: no bar above (a wide canvas is not centred vertically) | 16 checks |
| M2-04 | `canvas_layout.hpp`: fit: no bar at the left (a narrow canvas is not centred horizontally) | 17 checks |
| M2-05 | `canvas_layout.hpp`: fit: the height of a letterboxed picture rounds up | 4 checks |
| M2-06 | `canvas_layout.hpp`: fit: a narrow canvas is scaled by the widths | 34 checks |
| M2-07 | `canvas_layout.hpp`: whole_scale() is always true | 12 checks |
| M2-08 | `canvas_layout.hpp`: bar_right() is the left bar mirrored wrongly | 7 checks |
| M2-09 | `canvas_layout.hpp`: centred() does not halve the margin | 12 checks |
| M2-10 | `canvas_layout.hpp`: parse_aspect takes every shape that starts with 16: | 4 checks |
| M2-11 | `canvas_layout.hpp`: parse_aspect takes 4:3 with anything after it | 2 checks |
| M2-12 | `canvas_layout.hpp`: the refusal does not name the value | 18 checks |
| M2-13 | `canvas_layout.hpp`: the wide canvas is 1024 wide | 57 checks |
| M2-14 | `window_layout.hpp`: the window is the SMALLEST multiple that fits | 3 checks |
| M2-15 | `window_layout.hpp`: the window may be smaller than 1x | 2 checks |
| M2-16 | `window_layout.hpp`: the window is not centred horizontally | 4 checks |
| M2-17 | `window_layout.hpp`: the decoration on top is ignored | 3 checks |
| M2-18 | `window_layout.hpp`: the grid cell ignores the aspect | 3 checks |
| M2-19 | `window_layout.hpp`: the grid cell is as high as 4:3 whatever the aspect | 3 checks |
| M2-20 | `renderer.hpp`: nothing is placed: the picture has no origin | 12 checks |
| M2-21 | `renderer.cpp`: the clip is never the picture | 1 checks |
| M2-22 | `renderer.cpp`: set_picture does not restore the clip | 1 checks |
| M2-23 | `renderer.cpp`: a new frame does not restore the clip | 1 checks |
| M2-24 | `renderer.cpp`: clear_clip_rect clears the clip altogether | 1 checks |
| M2-25 | `renderer.cpp`: set_canvas_size keeps the picture | 2 checks |
| M2-26 | `renderer.cpp`: the screenshot is the whole window with the picture at its corner | 12 checks |
| M2-27 | `renderer.cpp`: the screenshot is the canvas size | 10 checks |
| M2-28 | `renderer.cpp`: the cached text is not placed | 1 checks |
| M2-29 | `renderer.cpp`: a new text is not placed | 2 checks |
| M2-30 | `renderer.cpp`: the digits are not placed | 2 checks |
| M2-31 | `renderer.cpp`: an RGBA image is not placed | 2 checks |
| M2-32 | `renderer.cpp`: a sprite is not placed | 2 checks |
| M2-33 | `renderer.cpp`: a fill is not placed | 4 checks |
| M2-34 | `renderer.cpp`: a frame is not placed | 2 checks |
| M2-35 | `renderer.cpp`: a clip rectangle is not placed | 2 checks |
| M2-36 | `renderer.cpp`: the parts of the world are not placed | 2 checks |
| M2-37 | `renderer.cpp`: the world clip is not placed | 2 checks |
| M2-38 | `application.cpp`: --aspect is not remembered as given | 3 checks |
| M2-39 | `application.cpp`: the settings beat the command line | 19 checks |
| M2-40 | `application.cpp`: the settings key is never read | 3 checks |
| M2-41 | `application.cpp`: a bad settings value is taken as 16:9 | 3 checks |
| M2-42 | `application.cpp`: the canvas is always 640 x 480 | 15 checks |
| M2-43 | `application.cpp`: the picture is not centred in the canvas at the start | 15 checks |
| M2-44 | `application.cpp`: the pointer is clamped to the layout, not the picture | 7 checks |
| M2-45 | `application.cpp`: Alt is not needed for the fullscreen key | 3 checks |
| M2-46 | `application.cpp`: a held Alt+Enter toggles on every repeat | 1 checks |
| M2-47 | `application.cpp`: the screens see the Alt+Enter key too | 2 checks |
| M2-48 | `application.cpp`: toggle_fullscreen does not leave fullscreen | 3 checks |
| M2-49 | `application.cpp`: toggle_fullscreen says the old state | 3 checks |
| M2-50 | `application.cpp`: the 16:9 window is not sized to the canvas | 2 checks |
| M2-51 | `application.cpp`: the grid cells are 4:3 whatever the aspect | 2 checks |
| M2-52 | `application.cpp`: the screens are not drawn into the picture | 2 checks |
| M2-53 | `application.cpp`: the plate is drawn into the picture | 1 checks |
| M2-54 | `application.cpp`: the cursor is drawn at the canvas, not into the picture | 2 checks |
| M2-55 | `application.cpp`: set_layout does not move the picture | 4 checks |

## M3: the wide match screen and the 16:9 default (what it does, what is left for M4 and M5)

Milestone M3 of the widescreen work (sections 52 and 70 of the plan; the owner's priority of 2026-10-02): the 16:9 picture of 960 x 540 gets its own frame, grown from the original's art, and a desktop game opens in it. **The original's 4:3 picture is unchanged**: the 307 fingerprints of suite 3.10 did not move, and a screenshot of the match at `--aspect 4:3` is, pixel for pixel, the one of the commit before (the frame-rate plate apart).

### What it does

- **The frame.** `include/ants_app/shell_layout.hpp` (pure, no SDL) classifies the 14 pieces of the animation `uishell` by what they are anchored to: Right (+dx), Bottom (+dy), and the six that grow by repeating ONE line of themselves (`ShellRule`: the cut column or row; `shell_spans` turns a piece into at most seven spans: before a cut, the repeated line stretched, after it, once for each of up to three cuts). With dx = dy = 0 every piece is one plain copy at its own place, which is what the original draws and what the 307 classic fingerprints see. `HUD::render_shell` draws the spans last piece first (the original's order); a piece in parts is drawn with the new `IRenderer::draw_sprite_region` (a part of a sprite stretched to a rectangle: a source one pixel wide over a wide destination repeats that column; the renderer's scaling is SDL's nearest filter, so a repeated line is that line, checked pixel for pixel on the real renderer).
- **The six cuts** (all measured on the art of `ants.chd`, the numbers are in the tests of suite 3.15 group "cuts"; the owner's choice for the right panel, 2026-10-02: "further down on the chat there's a spot that can repeat cleanly"):

| Piece | Cut | Why there | Spans |
|---|---|---|---|
| `x0y0` the top bar | column 140 (wider by dx) | columns 138 - 143 are identical; right of the black clock box (its edge is columns 129 - 132), left of the local score label | 3 |
| `x17y461` the bottom strip | **three cuts**, columns 15, 188 and 346 (wider by dx, shared in thirds, the left cut taking the remainder; down by dy) | columns 13 - 17 are the plain band left of the first score box's recess (edge at column 86), columns 186 - 190 the plain run right of the first box and left of the second team's label, columns 345 - 348 the one left of the third team's label: one cut in each gap, so the three boxes are spread over the strip (the owner: "can you expand between the scores so they're not all offset to the right?"; M3 had the one cut at column 15 and the boxes together at the right end); **never inside columns 458 - 482, which are the right panel's own fill** (52.P correction: a stretch there puts a flat block between the strip's end ornament and the panel) | 7 |
| `x0y22` the left strip | row 300 (taller by dy) | rows 294 - 314 are identical, below the horizontal rule (rows 260 - 266: it stays level with the chat header) | 3 |
| `x458y35` the strip between the map and the panel | row 322 = canvas y 357 | neighbouring rows differ by 3 and 0 pixels; the nearest ant decoration is 10 rows away | 3 |
| `wchat` the chat log's box | row 59 = canvas y 357 | a flat fill (rows 3 - 99 identical): the chat log takes the extra height | 3 |
| `x521y254` the right edge strip | row 103 = canvas y 357 | neighbouring rows differ by 5 and 4 pixels; the nearest ant decoration is 10 rows away (the row 50 that the owner rejected is beside one: the test asserts it fails the same rule) | 3 |

  The other eight pieces move: the three text boxes `wstatus` (right), `wtype` (right and down) and the minimap's bezel `x599y35`, the top of the panel `x458y22`, the status card `x480y126` and the chat header `x480y266` (right), the ant relief `x480y400` and the panel's bottom `x480y466` (right and down). Every piece of the right panel that spans canvas row 357 is cut there: exactly the three.
- **The result at 960 x 540**: the map view is 762 x 500 at (16, 21), the right panel is pinned to the right edge, the chat log is 161 px tall, the three score boxes of the bottom strip are spread over it (x 213, 468 and 722; M3 had them together at the right end), the top bar's score box and buttons follow the right edge. The composed frame is, pixel for pixel, the owner-approved mock-up (its one-line-repeat rule written out again in the test, independent of the header) at 960 x 540 and at five other sizes, and the real renderer draws it exactly (suite 3.15 groups "compose" and "renderer").
- **Everything the original draws with absolute numbers inside the panel moves with its anchor**: the pedestals' art (`PedestalSlot::draw` takes an offset), the glow, the egg tray, the Stop button, the lunchbox indicator and the hover / pressed art of the top bar's buttons by (dx, 0); the chat cover and the Send to buttons by (dx, dy). The status line, the chat log, the chat input and the score boxes were placed by M1.
- **Score boxes by slot, no new 4**: `ScreenLayout::score_slot(k)`: 0 is the local team's (top bar, right anchored), 1 - 3 the original's bottom slots (bottom anchored and **spread over the strip**: the strip is widened at three cuts, `ScreenLayout::cut_share` gives dx / 3 to each and the remainder to the leftmost, and a box and its label move by what the cuts left of the box add: 108, 214 and 320 at 960 wide, so the boxes are at x 213, 468 and 722, spaced 255 and 254 like the original's 149 and 148). The strip holds the original's three slots at every width (`bottom_slot_count()` is 3): the review fix removed the further slots that M3 had put one pitch (148) to the left of the first, which the spread leaves no room for (no further slot fits left of the first box at any width). More than four players is still the separate project: a strip with more slots would get one more cut for each, with dx shared equally between them, so the boxes stay evenly spaced.
- **The original's windows in a bigger picture.** The pages outside a match (the loading screen, the quick help at the start, the results; the setup screen and the room were pages in M3 and are the wide setup screen since the setup work: `include/ants_app/setup_layout.hpp`) are the original's 640 x 480 screens, centred in the canvas over a margin of the clay colour of the pages ((219, 75, 19), the loading screen's own fill: a window on a clay desktop; black was the other candidate, both screenshots were looked at). **The two windows that open DURING a match are drawn over the map view with the HUD around them, as the original's picture has them** (the review fix of M3 and the owner's decision; M3 filled the canvas with clay and drew a centred 640 x 480 page, which hid the whole HUD): the options window (the original's 442 x 440 card over its map view) is centred in the 762 x 500 view ((160, 30), `ScreenLayout::options_offset()`), the quick help (its 640 x 480 page) too ((77, 31), `quick_help_offset()`; both (0, 0) in the original's picture); they are drawn clipped to the map view (the options composite's dither shadow reaches beyond the card) and the HUD is drawn first and stays. The dialogs (quit, the three alliance dialogs, "get ready") are drawn under the dialog offset: the frame's centre goes to the centre of the map view ((137, 59) at 960 x 540, `ScreenLayout::modal_offset()`; (0, 0) in the original's picture, where the dialog is where the original puts it). The pointer is taken back to the windows' own numbers in every handler (press, release, hover of the buttons, the options' sliders, the quick help's Return) and in the 50 ms poll of `HUD::update` (`HUD::open_window_offset()` is the one place).
- **The picture of each screen.** The match is the whole canvas; every other screen (loading, quick help, setup and the room, results) is the original's 640 x 480 page centred in it, with the clay around it. `Application::picture_for_state` says which, `update_picture` follows every change of screen (the match starts, the results open, the way back, a new layout) and moves the pointer's numbers with the corner so that the pointer stays where it is on the canvas; the pointer starts in the middle of the picture that is up. A screen that no longer fills the canvas is drawn over the clay by `render_frame`.
- **Small maps.** A map that is smaller than the view on an axis (a 16 x 16 map is 512 px wide, the view 762) is centred in it with the black that the frame starts with around it and the camera is fixed on that axis (`ViewportCamera::centre_small_maps`, set by `Renderer::set_layout` for every layout but the original's: the original's camera keeps its rule, a small map at the corner, which the classic fingerprints pin). The cursor over the black is the plain pointer, a click there does nothing at all (no deselect, no order, no marker, a latched pedestal stays latched: `HUD::over_ground`; M3 let it deselect), the rubber band works across it, nothing that hangs over the map's edge is drawn onto the black (the world's clip is the part of the view that the map covers: `Renderer::map_view_rect`), the minimap's frame is the whole image on an axis where the view covers the map, the edge strips of an axis that cannot move show no arrow, the sound listener is the middle of the map.
- **The view size is read by**: the camera's limits and `center_on`, the renderer's clip and culling (M1), the edge strips (`edge_scroll_step`: the strips run along the edges of the whole 960 x 540 picture, the target point scales the pointer from the picture to the view), the minimap's drag (a square of half the view), the minimap's view frame (`HUD::render_radar`), the start view (`start_view_origin`), the rubber band's clamp, the cursor's and the clicks' screen to world conversions (`view().x / y`), Ctrl+H / Ctrl+N (`scroll_to_show`), the network overlay's box and the sound listener (`Application`). **For M4 (the wheel zoom)** these are the places that must tell "the rectangle of the view on the screen" from "the world extent that it shows": the camera clamp, the three edge-scroll functions, the frame of the minimap, the start view, `hud_input.cpp` lines that convert screen to world (`evaluate_cursor`, `pointer_click`, `pointer_release`, `pointer_right_click`, `band_rect`), the listener and `Renderer::render_*` (the culling margins, `world_to_screen`).
- **The flip.** `kPlatformDefaultAspect` (`canvas_layout.hpp`: 16:9 on a desktop, 4:3 under Emscripten) is the aspect that `Application::parse_arguments` puts into the config when nothing says otherwise; `--aspect` and the settings key `aspect` win (the key is read only when the command line did not give the aspect); a config made by hand (the tests') keeps `ApplicationConfig`'s own 4:3, so no test had to change to stay on the classic picture. **M5 (the web page) flips the web with that one constant** (and by letting `choose_aspect` read `?aspect=` / the settings key there: today the key is read in native builds only).
- **The four-window rig.** `start_game.sh` / `.bat` pass `--grid 2x2 --cell N` and nothing about the shape: with the new default each window is the largest 16:9 rectangle of its cell (on a 1080p display about 885 x 497 each; the cells of `grid_cell_window` were made for any canvas shape by M2). The start-script test checks that no window gets `--aspect` or `--window-size`.

### A known limit: seams at fractional scales, looked at again

The 1 px seams between sprites at a fractional scale (2560 x 1440 shows 960 x 540 at 2.667x) are a property of **SDL's software renderer** (the headless runs and the screenshot tool: it rounds every destination rectangle on its own): they are in the classic picture at 800 x 600 too. With the accelerated renderer (Metal on this Mac, a hidden window of 5120 x 2880 pixels = 5.333x, sampled across the map view) there is not one black pixel. The "render the canvas into a target texture and scale that once" fix was NOT done: it would draw the TrueType text at canvas resolution (its glyphs are rasterised at twice the size and filtered by the window scale, which is what keeps them sharp on a 4K screen), a visible loss for the case that has no problem. If a software-rendering machine ever matters, the fix is to render only the world and the frame into the target and the text afterwards.

### What is left (for M4 and M5)

- **M4 (the wheel zoom)**: see "The view size is read by" above; the HUD's page and dialog offsets do not depend on it. The fingerprint family "*.wide.*" is the oracle of the wide picture, as the classic family is of the original's.
- **M5 (the web page)**: the constant above, the container and its 16:9 frame, the pages' margin (clay) and the in-match windows are already the game's; `Renderer::save_screenshot`, `Alt+Enter` and the grid are native only.
- **Not done, by decision**: the original's loading screen and pages are not extended to the wide picture (they are centred, "the original pages themselves unchanged: fingerprints"); a monitor-shaped canvas (`--aspect` takes 16:9 and 4:3 only).

### Proof

- **Suite 3.15 `test_wide_hud`** (`tests/test_app/test_wide_hud.cpp`, 609 checks after the review fixes; 386 at M3): the anchoring model (each of the 14 pieces at 960 x 540 and at five other sizes: place, size, spans tiling it without a gap or an overlap), the cuts (every repeated line in a maximal run of identical lines of its own plain part, pinned runs 138 - 143, 13 - 17, 294 - 314; the panel's fill 458 - 482 shown to be identical too and refused; the right panel's three pieces cut at one canvas row, differing from their neighbours by at most 5 pixels and 8 or more rows from an ant decoration, the owner's rejected row 50 failing the same rule; every piece spanning canvas row 357 cut there), the composition (the spans against the mock-up's rule written out independently, pixel for pixel at seven sizes; each stretched piece at its original size equals the original; the original's picture is the 14 plain pieces), the real renderer (the frame at 960 x 540 and 1280 x 720 pixel for pixel in the HUD's colours; the origin of a window moves sprites, fills, frames and clips), the HUD (the frame's calls in order, 18 region calls, the panel's animations at their places + (dx, dy), the glow, the Stop button, the egg tray, the lunchbox, the chat cover, the hover and pressed art of the top bar), the score slots (the model, and the HUD's boxes and covers for six rosters and local colours in both pictures), the windows (the dialog and page offsets, the margin, the clip, the pointer in every handler: the quit dialog, the start dialog, the alliance dialogs, the options, the quick help), the camera (a big map, 16 x 16, 12 x 12, a map 1 px wider than the view), the edge strips (every one of the 518,400 pixels, the inner strips, the corners that cannot scroll, maps smaller than the view), the start view (six maps, every anchor), the minimap's frame, the small-map pointer (cursor, click, rubber band), the application (the picture of each screen and the pointer's move with it, the clay margin on the pages and in a match, the default and its options, the grid's window shape, the pointer over the bars of a 16:10 and a 21:9 window).
- **Suite 3.10 `test_view_fingerprint`**: 226 new fingerprints ("*.wide.*", golden numbers made at the commit of M3: 82 HUD draw-call scenes, 87 pointer and camera families (every pixel of the 960 x 540 picture for the edge strips from nine cameras and on small maps, the zones, the minimap, 13 cursor states, refined click sweeps), 57 pixel hashes of the real renderer (six maps, nine cameras of two, two synthetic small maps, whole application frames: pages and match)); the 307 classic numbers did not move; new self-checks for the recorder's two new calls.
- **Suites 3.13 and 3.14**: one assertion of 3.13 and the assertions of 3.14 that encoded M2's interim state were rewritten (see the CHANGELOG, "Rewritten tests"); nothing else changed.
- **Mutations** (one change at a time in a scratch copy, the six affected suites rebuilt and run, the file restored; the first run with 88, the second with the ones that had not compiled, the survivors and 29 more): 115 mutations of the sources (the model's anchors and cuts, the layout's slots and offsets, the camera, the renderer's part-of-a-sprite and origin, the HUD's moved animations, windows and hit tests, the application's pictures and the default); **110 are killed**, most of them by several suites (the new 3.15 and the wide fingerprints of 3.10; the classic fingerprints, 3.13 and 3.14 catch what touches the original's picture), and five survive for a reason: four because the picture of a screen is updated in more than one place on purpose (by the transition, by `update_results`, by `render_frame` and for every event): removing the end of a match's call (M3-A11), the per-event call (M3-A16) or one of the pair `return_to_map_select` / `enter_map_select` (M3-A12, M3-A13) leaves another to do it, and removing the pair (M3-AX4) or the nets together (M3-AX1, M3-AX5, M3-AX3) is killed; one (M3-A19) is unreachable, the replay callback of the results screen is never called by anything. The first run of 88 left 13 mutations to look at: five did not compile and were written again, and the others got their tests (the hover pictures of the top bar's buttons, of the options' and the quick help's Return and of the alliance dialog, the Stop button of a hill, the pointer's first place, `set_layout`'s picture, the way back from a running match, a map one pixel wider than the view, the part of a sprite under the picture's corner and the origin, a frame's reset of the origin, the pedestal's offset in both directions).

| # | Mutation | Killed by (suite: failing checks) |
|---|---|---|
| M3-S01 | `shell_layout.hpp`: top bar cut moved to column 130 (inside the clock box edge) | killed (3.15: 12, 3.10: 102, 3.14: 1) |
| M3-S02 | `shell_layout.hpp`: bottom strip cut moved to column 470 (the panel fill) | killed (3.15: 11, 3.10: 102) |
| M3-S03 | `shell_layout.hpp`: left strip cut moved to row 262 (the horizontal rule) | killed (3.15: 11, 3.10: 102, 3.14: 1) |
| M3-S04 | `shell_layout.hpp`: strip beside the map cut at row 300 (a decoration) | killed (3.15: 12, 3.10: 102) |
| M3-S05 | `shell_layout.hpp`: chat box cut at row 20 (not canvas row 357) | killed (3.15: 3, 3.10: 82) |
| M3-S06 | `shell_layout.hpp`: right edge strip cut at row 50 (the rejected row) | killed (3.15: 12, 3.10: 102) |
| M3-S07 | `shell_layout.hpp`: chat input box not anchored to the bottom | killed (3.15: 11, 3.10: 82) |
| M3-S08 | `shell_layout.hpp`: status box not anchored to the right | killed (3.15: 11, 3.10: 102) |
| M3-S09 | `shell_layout.hpp`: the panel bottom piece not anchored to the bottom | killed (3.15: 11, 3.10: 102) |
| M3-S10 | `shell_layout.hpp`: the minimap bezel not anchored to the right | killed (3.15: 11, 3.10: 102) |
| M3-S11 | `shell_layout.hpp`: a piece without a rule (name typo) | killed (3.15: 2, 3.10: 102) |
| M3-S12 | `shell_layout.hpp`: repeated column drawn extra_w wide instead of 1 + extra_w | killed (3.15: 16, 3.10: 102) |
| M3-S13 | `shell_layout.hpp`: the part after the cut read from column c | killed (3.15: 9, 3.10: 102) |
| M3-S14 | `shell_layout.hpp`: the repeated row is the row after the cut | killed (3.15: 8, 3.10: 102) |
| M3-S15 | `shell_layout.hpp`: no span is ever "whole" | killed (3.15: 17, 3.10: 179, 3.1: 15) |
| M3-S16 | `shell_layout.hpp`: bottom anchors do not move | killed (3.15: 17, 3.10: 102, 3.14: 1) |
| M3-S17 | `shell_layout.hpp`: right anchors move by dx / 2 | killed (3.15: 26, 3.10: 102) |
| M3-S18 | `shell_layout.hpp`: a taller piece grows by dy - 1 | killed (3.15: 20, 3.10: 102) |
| M3-L01 | `screen_layout.hpp`: page offset one pixel off | killed (3.15: 5, 3.10: 19, 3.1: 7) |
| M3-L02 | `screen_layout.hpp`: dialog offset centres the frame by its corner | killed (3.15: 4, 3.10: 14) |
| M3-L03 | `screen_layout.hpp`: the original picture has a dialog offset too | killed (3.15: 1, 3.10: 20, 3.1: 11) |
| M3-L04 | `screen_layout.hpp`: slot pitch 147 | killed (3.15: 1) |
| M3-L05 | `screen_layout.hpp`: the strip band starts at 200 | killed (3.15: 3, 3.13: 1) |
| M3-L06 | `screen_layout.hpp`: four bottom slots at least | killed (3.15: 2, 3.13: 1) |
| M3-L07 | `screen_layout.hpp`: a slot past the last is not clamped | killed (3.15: 2, 3.13: 2) |
| M3-L08 | `screen_layout.hpp`: further slot label gap 4 | killed (3.15: 2) |
| M3-R01 | `renderer.cpp`: the camera does not centre a small map in x | killed (3.15: 15, 3.10: 7) |
| M3-R02 | `renderer.cpp`: the camera does not centre a small map in y | killed (3.15: 3, 3.10: 4) |
| M3-R03 | `renderer.cpp`: a wide layout does not set the centring flag | killed (3.15: 1, 3.10: 4) |
| M3-R04 | `renderer.cpp`: draw_sprite_region reads one column too many | killed (3.15: 2, 3.10: 20, 3.14: 1) |
| M3-R05 | `renderer.hpp`: the origin is not added to y | killed (3.15: 4, 3.10: 4) |
| M3-R06 | `renderer.hpp`: set_origin forgets x | killed (3.15: 5, 3.10: 4) |
| M3-R07 | `renderer.cpp`: a camera of a map one pixel wider than the view is centred | killed (3.15: 1) |
| M3-H01 | `hud.cpp`: the frame is drawn first piece first | killed (3.15: 4, 3.10: 213, 3.1: 1) |
| M3-H02 | `hud.cpp`: left pedestal not moved | killed (3.15: 3, 3.10: 17) |
| M3-H03 | `hud.cpp`: right pedestal not moved | killed (3.15: 1, 3.10: 8) |
| M3-H04 | `hud.cpp`: egg tray not moved | killed (3.15: 1, 3.10: 2) |
| M3-H05 | `hud.cpp`: Stop button on a hill not moved | killed (3.15: 1, 3.10: 2) |
| M3-H06 | `hud.cpp`: Stop button of ants not moved | killed (3.15: 2, 3.10: 14) |
| M3-H07 | `hud.cpp`: lunchbox indicator not moved | killed (3.15: 1, 3.10: 1) |
| M3-H08 | `hud.cpp`: chat cover not moved down | killed (3.15: 1, 3.10: 3) |
| M3-H09 | `hud.cpp`: Send to buttons not moved down | killed (3.15: 1, 3.10: 99) |
| M3-H10 | `hud.cpp`: hovered Help label not moved | killed (3.15: 1, 3.10: 1) |
| M3-H11 | `hud.cpp`: pressed Options art not moved | killed (3.15: 1, 3.10: 4) |
| M3-H12 | `hud.cpp`: hovered Quit label not moved | killed (3.15: 1, 3.10: 1) |
| M3-H13 | `hud.cpp`: pedestal glow not moved | killed (3.15: 1) |
| M3-H14 | `hud.cpp`: dialogs not moved by the modal offset | killed (3.15: 4, 3.10: 13) |
| M3-H15 | `hud.cpp`: pages not moved by the page offset | killed (3.15: 3, 3.10: 8) |
| M3-H16 | `hud.cpp`: no clay margin around a page (a fill of no size) | killed (3.15: 5, 3.10: 8) |
| M3-H17 | `hud.cpp`: no clip of the page | killed (3.15: 2, 3.10: 7) |
| M3-H18 | `hud.cpp`: quit Yes hit without the modal offset (press) | killed (3.15: 1, 3.10: 2) |
| M3-H19 | `hud.cpp`: quit No hit without the modal offset (press) | killed (3.15: 1, 3.10: 2) |
| M3-H20 | `hud.cpp`: options press without the page offset | killed (3.15: 1, 3.10: 2) |
| M3-H21 | `hud.cpp`: options release without the page offset | killed (3.15: 1, 3.10: 2) |
| M3-H22 | `hud.cpp`: options hover without the page offset | killed (3.15: 2, 3.10: 1) |
| M3-H23 | `hud.cpp`: quick help press without the page offset | killed (3.15: 1, 3.10: 3) |
| M3-H24 | `hud.cpp`: quick help release without the page offset | killed (3.15: 1, 3.10: 2) |
| M3-H25 | `hud.cpp`: quick help hover without the page offset | killed (3.15: 2, 3.10: 1) |
| M3-H26 | `hud.cpp`: alliance press without the modal offset | killed (3.15: 1) |
| M3-H27 | `hud.cpp`: alliance release without the modal offset | killed (3.15: 1) |
| M3-H28 | `hud.cpp`: quit release without the modal offset | killed (3.15: 1, 3.10: 2) |
| M3-H29 | `hud.cpp`: quit hover without the modal offset | killed (3.15: 2, 3.10: 1) |
| M3-H30 | `hud.cpp`: alliance hover without the modal offset | killed (3.15: 2, 3.10: 1) |
| M3-H31 | `hud.cpp`: minimap frame ignores a map narrower than the view | killed (3.15: 2, 3.10: 4) |
| M3-H32 | `hud.cpp`: minimap frame ignores a map shorter than the view | killed (3.15: 1, 3.10: 3) |
| M3-H33 | `hud.cpp`: every other team in the first bottom slot | killed (3.15: 13, 3.10: 213, 3.13: 4, 3.1: 8) |
| M3-H34 | `hud_input.cpp`: the cursor over black beside a small map is not plain | killed (3.15: 5) |
| M3-H35 | `hud_input.cpp`: the right side of a small map counts as ground | killed (3.15: 1) |
| M3-A01 | `application.cpp`: the match uses the page picture | killed (3.15: 14, 3.10: 23, 3.13: 4, 3.14: 12) |
| M3-A02 | `application.cpp`: a page uses the match picture | killed (3.15: 8, 3.10: 8, 3.14: 5) |
| M3-A03 | `application.cpp`: the pointer does not follow a change of picture (x) | killed (3.15: 2) |
| M3-A04 | `application.cpp`: the pointer does not follow a change of picture (y) | killed (3.15: 2) |
| M3-A05 | `application.cpp`: no clay margin around a page of the screens | killed (3.15: 1, 3.10: 7) |
| M3-A06 | `application.cpp`: parse_arguments puts no default | killed (3.15: 5, 3.14: 2) |
| M3-A07 | `application.cpp`: choose_aspect forgets the config | killed (3.15: 24, 3.10: 31, 3.14: 24) |
| M3-A08 | `application.cpp`: the layout of the wide aspect is the classic one | killed (3.15: 14, 3.10: 22, 3.14: 11) |
| M3-A09 | `application.cpp`: enter_match does not update the picture | killed (3.15: 4, 3.10: 1) |
| M3-A10 | `application.cpp`: update_results does not update the picture | killed (3.15: 2) |
| M3-A11 | `application.cpp`: the end of a match does not update the picture | SURVIVES: redundant by design (update_results, render_frame and the next event update the picture too); killed together with the others by M3-AX5 |
| M3-A12 | `application.cpp`: return_to_map_select does not update the picture | SURVIVES: redundant pair with M3-A13 (enter_map_select updates it as well); the pair together is killed by M3-AX4 |
| M3-A13 | `application.cpp`: enter_map_select does not update the picture | SURVIVES: redundant pair with M3-A12; the pair together is killed by M3-AX4 |
| M3-A14 | `application.cpp`: render_frame does not update the picture | killed (3.10: 1) |
| M3-A15 | `application.cpp`: init does not update the picture | killed (3.15: 2) |
| M3-A16 | `application.cpp`: handle_events does not update the picture per event | SURVIVES: redundant by design (the transitions and the frame update the picture; this one is for a state change in the middle of a queue of events); killed together by M3-AX1 and M3-AX5 |
| M3-A17 | `canvas_layout.hpp`: the desktop default is the classic picture | killed (3.15: 1) |
| M3-A18 | `application.cpp`: set_layout does not update the picture | killed (3.15: 1) |
| M3-A19 | `application.cpp`: the replay does not update the picture | SURVIVES: unreachable (nothing calls the results screen's replay callback: dead code) |
| M3-A20 | `application.cpp`: the pointer starts at the layout centre, not the picture centre | killed (3.15: 1, 3.14: 1) |
| M3-S19 | `shell_layout.hpp`: the ant relief under the chat log not anchored to the bottom | killed (3.15: 11, 3.10: 102) |
| M3-S20 | `shell_layout.hpp`: the status card not right anchored | killed (3.15: 11, 3.10: 102) |
| M3-S21 | `shell_layout.hpp`: the top of the panel not right anchored | killed (3.15: 11, 3.10: 102) |
| M3-S22 | `shell_layout.hpp`: the chat box not right anchored | killed (3.15: 11, 3.10: 102) |
| M3-S23 | `shell_layout.hpp`: the right edge strip not right anchored | killed (3.15: 11, 3.10: 102) |
| M3-S24 | `shell_layout.hpp`: the strip beside the map not right anchored | killed (3.15: 11, 3.10: 102) |
| M3-S25 | `shell_layout.hpp`: the chat header not right anchored | killed (3.15: 11, 3.10: 102) |
| M3-S26 | `shell_layout.hpp`: the ant relief under the chat log not right anchored | killed (3.15: 11, 3.10: 102) |
| M3-R08 | `renderer.cpp`: draw_sprite_region without the HUD team colour | killed (3.15: 2, 3.10: 20, 3.14: 1) |
| M3-R09 | `renderer.cpp`: draw_sprite_region ignores the picture and the origin | killed (3.15: 2) |
| M3-R10 | `renderer.hpp`: the camera centres small maps by default (the original's camera) | killed (3.15: 2, 3.10: 3) |
| M3-R11 | `renderer.cpp`: the original's layout keeps the centring flag | killed (3.15: 1) |
| M3-R12 | `renderer.cpp`: a frame does not clear the origin | killed (3.15: 1) |
| M3-P01 | `pedestal.cpp`: the pedestal ignores dx | killed (3.15: 5, 3.10: 17) |
| M3-P02 | `pedestal.cpp`: the pedestal ignores dy | killed (3.15: 1) |
| M3-H37 | `hud.cpp`: the page margin is drawn in the original's picture too | killed (3.15: 1, 3.10: 7, 3.1: 1) |
| M3-H38 | `hud.cpp`: the page is clipped in the original's picture too | killed (3.15: 1, 3.10: 7) |
| M3-H39 | `hud.cpp`: quit hover uses the dialog offset of the other axis | killed (3.15: 1, 3.10: 1) |
| M3-H40 | `hud.cpp`: the dialogs' offset is the page offset | killed (3.15: 4, 3.10: 13) |
| M3-H41 | `hud.cpp`: a page is drawn with the dialog offset | killed (3.15: 3, 3.10: 8) |
| M3-A21 | `application.cpp`: the settings key beats --aspect | killed (3.15: 1, 3.14: 2) |
| M3-A22 | `application.cpp`: the margin fill is never drawn (condition) | killed (3.15: 1, 3.10: 7) |
| M3-AX1 | `application.cpp`: the three per-frame and per-event updates of the picture removed together | killed (3.15: 2, 3.10: 1) |
| M3-AX2 | `application.cpp`: the updates at the transitions removed together (enter_match, the end of a match, the way back, init, set_layout) | killed (3.15: 8, 3.10: 1, 3.14: 2) |
| M3-AX3 | `application.cpp`: no update of the picture anywhere | killed (3.15: 16, 3.10: 23, 3.13: 4, 3.14: 12) |
| M3-AX4 | `application.cpp`: the way back to the setup screen does not update the picture in either place (return_to_map_select and enter_map_select) | killed (3.15: 2) |
| M3-AX5 | `application.cpp`: neither the end of a match nor update_results nor the events update the picture | killed (3.15: 2) |

### Review fixes of the widescreen work (M3), and the score boxes spread over the strip

An independent review of M1 - M3 (drivers that click every control through real frames, maps of 12 and 15 tiles, a four-team network match with a 16:9 and a 4:3 machine, 60 mutations of its own; scratch only) found one high, two medium and five low findings and one note, and the owner asked for the score boxes to be spread. Each finding has a test that fails without its fix (the mutations are below); the classic picture did not move (the 307 classic fingerprints and every other suite are unchanged).

| Finding | What was wrong | The fix | Proof |
|---|---|---|---|
| HIGH-1 | In the 16:9 match Return and the ON / OFF switches of the options window and the quick help's Return could not be clicked | `HUD::update` re-fed the open window the raw pointer every 50 ms (the INPUT task's poll) without the window's offset, which the event handlers subtract: the poll took the pointer off the control and cancelled the press. `HUD::open_window_offset()` is the one place; the poll subtracts it | 3.15 `window-controls`: every control of both windows is pressed, polled with `HUD::update` and released, in both pictures, and does nothing at the old place; the review's own survivor (n05) is killed here |
| MEDIUM-2 | The ping / delay readout stood on the third score box in a four-team 16:9 network match | the match's limit was `LATENCY_LEFT_LIMIT_MATCH` = 460, a number of the original's picture; it is `ScreenLayout::score_row_right()` now (the last bottom box + 58: the right edge of its cover and 2 px; 460 classic, 780 at 960 x 540) and `latency_left_limit` takes the layout | 3.15 `latency`: two, three and four teams in classic, 960 x 540, 1280 x 720, 1920 x 1080 against the boxes and covers that the HUD draws (with the real font); the control with 460 overlaps; 3.6 N5.29b: the application's own frame (`Application::last_latency_layout`) |
| MEDIUM-3 | The first window of a 16:9 game was the largest WHOLE multiple of 960 x 540: 960 x 540 points on a 1800 x 1130 display | the owner's decision: the largest scale in steps of 0.5 (`default_canvas_window`, 1.5x = 3x in pixels on a Retina display), at least 1x, fullscreen unchanged | 3.14 `window`: a table of six displays (1800 x 1130, 1440 x 875, 1920 x 1040, 2560 x 1400, 3840 x 2100, 1366 x 728) with and without a title bar, the width and the height limit one pixel either side of every step (the review's survivor n07), centring |
| LOW-1 | On a small map an object at the edge drew onto the black around the map (the clip was the view) | the clip is the view and the map intersected (`Renderer::map_view_rect`) | 3.15 `small-map-draw`: the rectangle for five map sizes, ants half out at the edges and corners of a 15 x 15 world, whole application frames of the review's 15 x 15 level (and an 18 x 31 one) cut from TINY.LVL |
| LOW-2 | Options hid the whole HUD in 16:9 (a clay fill and a centred 640 x 480 page) while in the original's picture the window sits over the map view and the panel stays | the options window (its 442 x 440 card) and the quick help of a MATCH are centred in the map view with the HUD around them, clipped to the view: `ScreenLayout::options_offset()` (160, 30) and `quick_help_offset()` (77, 31); the pages outside a match are unchanged | 3.15 `windows`, `window-controls`, `margin`: the offsets, the origin and clip of the drawing, the HUD drawn first, the card's pixels at equal distances from the view's edges, every control clicked at its place |
| LOW-3 | The cursor vanished on a page switch (a pointer left of x = 160 became (-60, 240)) | `Application::update_picture` holds the shifted pointer in the new picture | 3.15 `screens`: seven pointer positions around a page |
| LOW-4 | A `--fullscreen` start kept the config's 1280 x 960: Alt+Enter gave a 4:3-shaped window | a game of the wide aspect gets the canvas's default window also when it starts in fullscreen | 3.14 `app`: the window size of a fullscreen start, with `--window-size`, and the original's aspect keeping 1280 x 960 |
| LOW-5 | A click on the black of a small map deselected | `HUD::over_ground`: a click, a right click and a right press that began on the black do nothing at all (no deselect, no order, no marker, a latched pedestal stays) | 3.15 `small-map`, `small-map-rows` (a 12 x 12 map: the pointer's boundary rows and columns on all four sides: the review's survivors n12) |
| INFO | `--window-size` silently ignored `WxH` | `parse_window_size`: `WxH` or `W,H`, at least 320x240; anything else is refused with a message and the game does not start | 3.14 `window-size` |

**The score boxes, spread.** The owner (2026-10-02): "Can you expand between the scores so they're not all offset to the right?" The bottom strip's extra width dx is shared over THREE plain cuts of its art instead of one: the piece columns 15 (as before: the band left of the first box), 188 (the plain run 186 - 190 right of the first box and left of the second team's label) and 346 (the run 345 - 348 left of the third team's label), in thirds with the remainder to the left cut (`ScreenLayout::cut_share`, one rule for the frame and the slots). At 960 x 540 (dx = 320) the cuts add 108, 106 and 106 and the boxes are at x **213, 468 and 722** (255 and 254 apart; the original's own gaps are 149 and 148); the labels stay with their boxes; each cut column is identical to its neighbours on all 19 rows (suite 3.15 `cuts` checks every cut, and that one cut lies in each gap between the recesses). `ShellSpans` has seven spans for the strip; the reference mock-up (`draw_multi`) is written out again in the test and the composed frame equals it pixel for pixel at seven sizes. M3's further slots (one pitch of 148 left of the first box, five bottom slots at 960 wide) have no room once the boxes are spread and were removed: every width has the original's three bottom slots; a strip for more slots would get one more cut for each, the extra width shared equally.

**Fingerprints.** 105 of the 226 "*.wide.*" fingerprints were measured again (the 307 classic ones and the other 121 wide ones did not move): all of them draw the bottom strip (seven draw calls instead of three, the boxes at x 213 / 468 / 722), and seven of them also the options window or the quick help in its new place (`hud.wide.options*` 3, `hud.wide.quickhelp*` 3, `ptr.wide.click.dialog.quickhelp`). By group: `hud.wide.chat` 8, `dialog` 4, `fog` 1, `marquee` 3, `minimap` 15, `options` 3, `pedestal` 3, `quickhelp` 3, `quit` 3, `scores` 10, `sel` 14, `start` 8, `status` 1, `topbar` 6, `ptr.wide.click` 1, `px.wide.app` 22. The pointer sweeps, the cameras and the world pixels (including the two small-map worlds) did not change.

**Mutations of the review fixes** (one change at a time in a scratch copy, the suites that can see it rebuilt and run, the file restored; the application suites 3.1 - 3.6 and 3.10 - 3.15): 67, all killed - 64 by tests (the number in brackets is the first suite that failed, in the order 3.15, 3.14, 3.13, 3.2, 3.5, 3.4, 3.1, 3.10; the first run of 14 also ran the others, hence two suites in some rows) and three at the build by the `static_assert` that keeps the original's limit of 460 (each of them has a variant for the wide picture alone that the tests kill: R15 - R17). One more mutation survived at first (a second `x` in `--window-size 1280x720x2` accepted: the number test refuses it anyway, so the extra check was dead code and was removed); the review's own survivors n05, n07 and n12 are R01 / R02, R18 and R41 here.

| # | Mutation | Killed by (suite) |
|---|---|---|
| R01 | HUD::update feeds the options window the raw pointer (the review finding) | killed (3.15) |
| R02 | HUD::update feeds the quick help the raw pointer | killed (3.15) |
| R03 | The options poll uses the quick help window's offset | killed (3.15) |
| R04 | The quick help poll uses the page offset (160, 30) instead of its own (77, 31) | killed (3.15) |
| R05 | The quick help uses the options window's offset everywhere | killed (3.15, 3.10) |
| R06 | The quick help release without the window offset | killed (3.15, 3.10) |
| R07 | The options release without the window offset | killed (3.15, 3.10) |
| R08 | The options press without the window offset | killed (3.15, 3.10) |
| R09 | The options hover without the window offset | killed (3.15, 3.10) |
| R10 | The match limit is the original's 460 in every layout (the review finding) | killed (3.15) |
| R11 | The application passes the original's layout | killed (3.6) |
| R12 | No margin right of the last cover | killed at the build: the `static_assert` that the original's picture keeps its limit of 460 |
| R13 | The cover reaches 54 right of the box (the box only) | killed at the build: the `static_assert` that the original's picture keeps its limit of 460 |
| R14 | The row ends at the second box | killed at the build: the `static_assert` that the original's picture keeps its limit of 460 |
| R15 | No margin right of the last cover in a wide picture only | killed (3.15) |
| R16 | The cover reaches 54 in a wide picture only (the box, not its cover) | killed (3.15) |
| R17 | The row ends at the second box in a wide picture only | killed (3.15) |
| R18 | The width's share one half step too many (the review's n07) | killed (3.14) |
| R19 | The height's share one half step too many | killed (3.14) |
| R20 | The smallest window is 0.5x | killed (3.14) |
| R21 | The larger of the two limits | killed (3.14) |
| R22 | Whole multiples only (the old rule) | killed (3.14) |
| R23 | Whole multiples in height only | killed (3.14) |
| R24 | Not centred in x | killed (3.14) |
| R25 | The title bar not counted | killed (3.14) |
| R26 | A game that starts in fullscreen keeps the config's 4:3 window (the review finding) | killed (3.14) |
| R27 | The canvas's width and height swapped | killed (3.14) |
| R28 | Render_world clips to the whole view (the review finding) | killed (3.15) |
| R29 | The map's left edge does not clip | killed (3.15) |
| R30 | The map's top edge does not clip | killed (3.15) |
| R31 | The map's right edge does not clip | killed (3.15) |
| R32 | The map's bottom edge does not clip | killed (3.15) |
| R33 | The map's left edge one pixel off | killed (3.15) |
| R34 | The map's right edge one pixel short | killed (3.15) |
| R35 | The shifted pointer is not clamped in x (the review finding) | killed (3.15) |
| R36 | The shifted pointer is not clamped in y | killed (3.15) |
| R37 | The clamp's right limit one too far | killed (3.15) |
| R38 | The clamp's bottom limit one too far | killed (3.15) |
| R39 | A click on the black deselects again (the review finding) | killed (3.15) |
| R40 | A right press on the black orders | killed (3.15) |
| R41 | One row of ground above the map (the review's n12) | killed (3.15) |
| R42 | One column of ground left of the map | killed (3.15) |
| R43 | One row of ground below the map | killed (3.15) |
| R44 | One column of ground right of the map | killed (3.15) |
| R45 | The cursor over the black is the move cursor | killed (3.15) |
| R46 | Only the comma separates (the old behaviour), first occurrence | killed (3.14) |
| R47 | The minimum width one less | killed (3.14) |
| R48 | The minimum height one less | killed (3.14) |
| R49 | A refused --window-size is silent (the old behaviour) | killed (3.14) |
| R50 | The largest size one more | killed (3.14) |
| R51 | The options window is not moved | killed (3.15) |
| R52 | The quick help sits at the page offset | killed (3.15) |
| R53 | The window is not clipped to the map view | killed (3.15) |
| R54 | No clip at all | killed (3.15) |
| R55 | Centred in x only | killed (3.15) |
| R56 | The remainder goes to the last cut | killed (3.15) |
| R57 | A box does not move by the cut left of it | killed (3.15) |
| R58 | The third cut at column 345 | killed (3.15) |
| R59 | The second cut at column 190 (the run's edge) | killed (3.15) |
| R60 | Two cuts only (the third box stays at the right end) | killed (3.15) |
| R61 | The second cut at column 150 (inside the first box's label band) | killed (3.15) |
| R62 | A cut column is drawn twice | killed (3.15) |
| R63 | Every cut takes the share of the first | killed (3.15) |
| R64 | The label's left edge does not move | killed (3.15) |
| R65 | Every box moves by the first cut | killed (3.15) |
| R66 | The thirds rounded up | killed (3.15) |
| R67 | Only the first cut of a piece is used | killed (3.15) |

## Integration of the 16:9 pieces (M1 - M3, the web page, the wide setup screen): the options window's dim

The four finished pieces were assembled on v0.0.98 (documents, one header comment and two tests conflicted; the code merged cleanly and every suite of each piece passes on the whole).
One thing did not survive the meeting and is fixed in the assembled tree: the options window's dim of the picture.

**What the original does** (measured on `ants.chd`, the animation `op_screen`, 210 pieces drawn last piece first): 25 pieces are checker dither (`dith100x` 100 x 20, `dith100y` 20 x 100,
`dith200` 200 x 200: palette index 175, the dark red (183, 11, 27), at every pixel of the piece where x + y of the piece is even, transparent elsewhere). They are laid as the left strip
(x -3 .. 16), the top strip (y 0 .. 19), the bottom strip (y 460 .. 479) and the whole right panel (x 459 .. 658, from y 0 to 479; the pieces at y 400 and below are 100 wide at x 459 and 539,
so the last column of a 640 x 480 picture, x = 639, is left out below y = 400: an arithmetic slip of the original's data that the remake keeps). The undimmed card is 442 x 440 at the
window's own (17, 20); the card's art reaches x = 459 (its right edge, `dfram3`), which the panel's dither overlays on every second pixel. In window numbers the dithered pixels are exactly
the pixels with x + y odd (every piece starts at an odd x + y: (-1, 0), (-3, 20), (459, 0) ...): the classic picture of the options window is the oracle, `test_wide_hud` group `options-dim`.

**What the first 16:9 version did**: the window is moved to the middle of the 762 x 500 map view by `ScreenLayout::options_offset()` ((160, 30)) and its pieces are clipped to the view (a window's
pieces reach beyond the card), so the dither started at x = 157 (the left strip moved) and stopped at the view's right edge: the 141 pixels of the view left of the strip, the strips above
and below the view, the frame (top bar, left strip, bottom strip) and the whole right panel stayed bright, while the original dims all of them.

**The fix**: `HUD::render_options_dim` (called before the window, only when the window has an offset) dims what the original dims relative to the card: the whole picture outside
`ScreenLayout::options_card()` = the window's (17, 20, 442, 440) moved by the offset, in four disjoint bands (above, below, left and right of it), each band tiled with `dith200` under its own
clip, the tiles starting at the window's first top strip piece ((-1, 0) of the window, so the dithered pixels are the same ones whatever the offset is: an odd offset keeps the phase); the
window's own clipped pieces overlap them with the same pixels. The original's own picture is untouched (the window's pieces do the whole dim there: the HUD draws no band) and its 307
fingerprints did not move; four wide fingerprints did, on purpose (`hud.wide.options.open`, `.changed`, `.hover_return`, `px.wide.app.match.options_page`).

| # | Mutation of `HUD::render_options_dim` / `ScreenLayout::options_card` | Killed by (suite) |
|---|---|---|
| I01 | The call is removed (the first version's behaviour) | killed (3.15 `options-dim`) |
| I02 | The tiles start at x = window offset (the phase is the other one of the checker) | killed (3.15) |
| I03 | The tiles start one row lower | killed (3.15) |
| I04 | The bottom band starts a row too high (over the card) | killed (3.15) |
| I05 | The card is 441 wide (the right band starts a column early) | killed (3.15) |
| I06 | The right band is not drawn | killed (3.15) |
| I07 | The tile step is 100, not 200 | killed (3.15) |
| I08 | The classic picture dims too | killed (3.15) |

**Suites renumbered**: the wide setup suite is 3.16 and the map preview suite is 3.17 (3.14 and 3.15 on their own branch, made before the widescreen suites 3.13 - 3.15 existed).

**Two tests that pinned what the pieces change together**: `test_wide_hud` `[screens]` (the way back from a running match to the setup screen: the setup screen is the whole canvas
of the wide picture and no page, so the pointer stays where it is; the results page, which is a page, still clamps it) and `test_start_menu_app` A2.2 (`click_fog` takes the fog button's own
rectangle: a command line is the 16:9 picture by default, whose setup screen has its buttons elsewhere than the original's page).

## M4: the mouse-wheel zoom (what it does, what is left for the touch work)

Milestone M4 of the widescreen work (sections 52 and 70 of the plan): the wheel over the map view zooms between 0.5, 1 and 2 towards the pointer. **At the zoom 1 nothing changed**: the 551 fingerprints of suite 3.10 (classic, wide, start menu) did not move, and no existing test was rewritten. M0's list of the places that read the view's size (`The view size is read by` in M3 above) is what M4 separated: the view's rectangle ON THE SCREEN (`ScreenLayout::view()`, unchanged) from the WORLD that it shows (`view / zoom` world pixels).

### What it does

- **The model** (`include/ants_app/view_zoom.hpp`, pure, no SDL): `kLevels` 0.5 / 1 / 2 (powers of two: every number is exact in a float and a double), `visible(len, z)` (the world pixels that `len` screen pixels show, rounded up), `grid(z)` (the camera origin's grid, ONE SCREEN PIXEL: 2 world pixels at 0.5, 1 at 1, 0.5 at 2), `snap`, `world_at` / `world_edge_up` (screen offset to world pixel: the left / top edge rounds down, the right / bottom edge of a rubber band up), `Limits` (`any()` or `no_zoom_out()`), `offered` / `step` / `level_for_match`, `clamp_origin`, `zoomed` (the camera after a zoom that keeps the point under the pointer), `wheel_amount`, `WheelAccumulator`, `level_name`, `parse_level`.
- **Anchoring.** `origin' = origin + anchor / zoom - anchor / zoom'`, snapped to the new zoom's grid and clamped. The snap is the only error: at most half a screen pixel; for 1 -> 2 it is exactly zero; for 1 -> 0.5 it is zero at an even offset and one world pixel off at an odd one (a 2-pixel grid); for 2 -> 1 from a half-pixel origin it is half a world pixel. The clamps come after the anchor, so at the edge of a map the point under the pointer moves (nothing beyond the map is shown). A map smaller than the view on an axis is centred and fixed there (M3's rule, now with the visible extent), truncated toward zero as the original's integer division does.
- **Which levels are offered** (`zoom::offered`): level 1 always (it is the original's picture); level 2 when the limits allow; level 0.5 when the limits allow it AND the level above it does not already show the whole map (a 12 x 12 map in the classic view is 384 px: at 1 the view of 442 x 440 shows all of it, so 0.5 would only add black). Limits: a local game and a game with bots `any()`, a match of the network (`net()->active()`, a server's room or a LAN game, host or guest) `no_zoom_out()`: **0.5 would show more of the map than the other players see**. A match of the network that starts resets a remembered 0.5 to 1 (`apply_match_zoom`, called by `init` and `load_match`) without changing the remembered level, so the next local game gets it back.
- **The wheel.** `Application::handle_mouse_wheel`: SDL's `y` / `precise_y`, `direction == SDL_MOUSEWHEEL_FLIPPED` undone (see below), added up by `WheelAccumulator` (a notch is a step; 0.4 + 0.4 + 0.4 is one step with 0.2 left; more than 500 ms of pause forgets the left-over; a reversal starts from nothing), then `step_zoom(direction, mouse_x, mouse_y)`. **Gate** (`view_zoom_allowed(x, y)`): a renderer, the state Playing, no results (scorecard) open, the pointer inside the window, `HUD::view_zoom_allowed()` (no dialog or page open: `!is_modal_open()`; no captured press: `!is_input_captured()`; no chat log drag), and the pointer over the map view (`HUD::over_map`). The **middle button** is swallowed by `handle_mouse_button` before the HUD sees it (the original has no use for it) and a press over the map view sets the zoom to 1 towards the pointer. Typing in the chat box is deliberately not a block: the chat box is always active in the original, and a player who types can still look around.
- **Direction.** `wheel_amount(y, precise_y, flipped)`: up (rolled away from the user) is positive: zoom in. `SDL_MOUSEWHEEL_FLIPPED` says the system already inverted the numbers (macOS "natural scrolling", Windows and Linux touchpads set to it) and that a program that wants the physical direction multiplies by -1; the code does that, so the physical direction decides. This is a judgment call (the task said "SDL flipped direction is honoured"): the physical direction decides on a native build whatever the system's setting is. On the web the browser reports deltas that it has already adjusted to the user's setting, and the code cannot know the physical direction there; that part is reasoned, not tested in a browser. If the owner prefers the system's direction on native too, it is the one line `return flipped ? -amount : amount;` of `wheel_amount` (and the tests of the group "wheel", which spell out both).
- **The camera.** `ViewportCamera` gained `zoom`, `visible_w / visible_h`, `world_x_at(offset)` / `world_x_edge(offset)` (and y), `origin_screen_x / y`, `centre_world_x / y`, `scroll_screen(dx, dy)` (the edge scroll's whole screen pixels), `set_origin`, `set_zoom`. `clamp_to_bounds` keeps the exact old arithmetic at zoom 1 (no change for the classic and wide pictures); `center_on`, `world_to_screen` and `screen_to_world` take the zoom into account. **`Renderer::world_view()`** replaced `layout_.view()` in all world code (culling of terrain, objects, ants, effects, fog, markers, bubbles; the clip), and `map_view_rect` is zoom-aware.
- **Drawing** (`Renderer::begin_world_target`, `end_world_target`). At zoom 1 the world is drawn directly into the view, as before. At another zoom (or with the test hook `set_force_world_target(true)`) the pass camera is a camera of zoom 1 whose view is an offscreen target of `visible + 1` pixels (the extra row and column cover a half-pixel origin); the target is cleared to black, the world is drawn into it exactly as at zoom 1 (so every sprite, the fog's autotiles and the sorting are the original's), and it is copied into the view: nearest filter at 2, linear at 0.5. The copy is one step when the copy is exactly the view; when the origin is a half pixel or the size is odd it goes through `scaled_target_` and a 1:1 crop (SDL's software renderer rounds the source of a clipped scaled copy, which shifted the picture by a pixel). The hit point digits (Ctrl+L) are queued during the pass and drawn after the copy at one size (queued only when the real zoom is not 1, so the zoom 1 path is untouched); the tile grid overlay (a debug aid) is drawn on the screen after the copy and follows the zoom; the cursor, rubber band and the frame are screen items. If the target cannot be made, the zoom 1 picture is drawn and the camera is reset to 1 (the player never sees a zoom that is not drawn); `zoom_kept` is read before `begin_world_target` resets the pass camera.
- **Edge scroll, arrows, minimap.** `edge_scroll_step_px` and `minimap_scroll_step_px` (screen pixels; the older functions are the same numbers times 32 for the tiles form), `minimap_point_px`, `start_view_origin(..., zoom)` (the visible extent); `HUD::edge_step(camera, ...)` and `HUD::input_tick` scroll with `camera.scroll_screen` so the same distance on the screen is covered at every zoom, the arrows (cursor choice) test what the camera can still do in screen pixels, `HUD::render_radar` draws the frame of the visible world, a minimap press centres the visible world, and `Ctrl+N` / `Ctrl+P` and the sound listener use the centre of the visible world. The cursor, clicks, orders, the rubber band and the hill brackets convert through `world_at` / `world_edge_up`.
- **Settings and command line.** `ApplicationConfig::zoom` / `zoom_given`, `--zoom`, the key `zoom` (`choose_zoom`: the command line wins, then the settings file; a value that is not exactly 0.5, 1 or 2 is reported on stderr and ignored; the command line refuses it). `set_zoom` persists the level (`config_store_.set_string`) and `zoom_wanted_`; the reset of a network match is not persisted.
- **The API for touch** (all public): `Application::set_zoom(level, anchor_x, anchor_y)` returns whether the level is offered now and sets the camera with the anchored origin; `step_zoom(direction, anchor_x, anchor_y)`; `zoom()`; `zoom_levels()` (the offered levels, ascending); `zoom_limits()`; `view_zoom_allowed(x, y)`; `remembered_zoom()`. A pinch calls `set_zoom` (or `step_zoom`) with the midpoint of the fingers in canvas pixels; the HUD's own gate is the same one that the wheel uses.
- **The web page** (`web/shell.html`): `canvas.addEventListener('wheel', e => e.preventDefault(), {passive: false})` so that the wheel never scrolls the page; a trackpad's pinch arrives as ctrl + wheel events (Chrome, Firefox, Edge), which the same listener cancels (no browser page zoom); Safari's `gesturestart / gesturechange / gestureend` are cancelled and the scale change is turned into synthetic ctrl + wheel events (a change of 25 % in scale is one notch of 100 delta units). SDL's Emscripten handler for `wheel` already returns "handled". **Checked by reading** (the base page is 4:3 and M5 reshapes it); no browser was driven for M4.

### Known limits

- **0.5 at a fractional window scale.** The canvas is scaled into the window by SDL (M2); the half-size picture is smoothed by SDL's linear filter in the copy and then scaled again, so the seams of M2 and M3's "known limit" apply as they did (a software-renderer property of fractional scales).
- **0.5 is a smoothed picture, not the original's art.** The world pass draws at zoom 1 and the copy halves it: sprites are blended with their neighbours. This is how a zoom-out of pixel art looks; the digits and the frame stay crisp.
- **0.5 on the accelerated renderer is checked less closely than on the software renderer.** Suites 3.19 and 3.20 run the software renderer (SDL's dummy video driver) and compare the 0.5 picture with the 2 x 2 average within one level; the zoom 2 picture of the accelerated renderer (Metal, a hidden window of 3840 x 2160 pixels, a scratch tool that is not in the repository) was compared with the enlarged zoom 1 picture and equals it (0 of 6,096,000 window pixels differ, at a whole and a half-pixel origin), and its 0.5 picture was looked at (screenshots) and timed, not compared pixel by pixel.
- **No keyboard zoom.** The original has no arrow-key or keyboard scroll and the remake none; M4 added only the wheel, the middle button and the API. A keyboard zoom, if wanted, is `step_zoom` from a key handler.
- **Not zoomed**: the minimap (it shows the whole map), the frame and every window, the hit point digits' size, the cursors, the status line.

### What is left (touch, M5)

- **A pinch** calls the API above; the gestures (two-finger scroll, a long press for the right button) are not built.
- **The web page**: checked in a real browser since the zoom was brought onto v0.0.99's 16:9 page (`tests/scripts/web_aspect_check.py --wheel`, see "M4 on v0.0.99" below); the page must keep the listeners.

### The ants in the screenshots (a question from the review of the first pictures, answered)

The first screenshots of the zoom (`ants --headless --map ... --screenshot ... --frames 12`) showed the hit point digits of the ants where they stand but not the ants. **That is not a regression**:

- **Cause.** That picture is taken before the first simulation tick. The headless frames take far less than the 50 ms of a tick, so the clock still reads 10:00 (a run that happens to be slower shows 9:59 and the ants: two of my first runs differed in exactly that). Until the first tick an ant has no animation clip: `draw_single_ant` has no frame to draw, while its digits (Ctrl+L, drawn from the same list of ants) are. So the picture has the digits and no sprites.
- **Evidence.** 42ef62d (the base) was built and the same command taken with it and with the tip, three times each: the six pictures are identical pixel for pixel, digits without ants included. A match that has ticked (the match-start modal dismissed, 40 frames of 16 ms: tick 3, 9:59) is pixel for pixel the same at the zoom 1 on 42ef62d and on the tip for GAUNTLET, TINY and SMALL in the 16:9 and the classic picture (six pairs); the M3 screenshot of the start view was taken that way (9:59). The ticked pictures at 0.5 and 2 show the ants.
- **Why no test pinned it, and what pinned ants already.** The renderer-level scenes do draw ants (`populate` gives each a clip): `px.world.*` of suite 3.10 and `zoom.world.*` of 3.20, the groups "pass" and "out" of 3.19. They killed every "no ant at a zoom" mutation I tried (RN40 - RN44, RN46 were run against the suites before the new tests: all killed). What no test had was an application frame WITH ants: the application frames of 3.10 (`px.app.match.*`) and 3.20 (`zoom.app.*`) are taken at once after the start, at tick 0, so they show digits and no sprites (they are kept: they pin the frame as the application draws it at the start).
- **Added.** In 3.20, `zoom.app.ants.{classic,wide}.{GAUNTLET,TINY}.z{0.5,1,2}` (12 fingerprints, the zoom 1 ones deliberately): whole frames after six ticks, each with a check that more than 300 pixels of the view differ from the frame before the first tick (the ants are in the picture). In 3.19, the group "ants": a real match after six ticks, the camera put at an ant of the local player; at the zoom 1 the sprites make a difference to the view that the same world without ants does not have, the zoom 2 picture is the nearest enlargement of the zoom 1 picture (ants included), and at 2 and 0.5 the ants are drawn; and in "pass" a check that the ants of the renderer scene are drawn. `test_zoom_view --only NAME` runs a single group (a diagnosis aid). Under RN40 - RN44 the new tests fail on their own (`--only ants` of 3.19 and `--only zoom.app.ants` of 3.20), RN46 is killed by the new fingerprints and by the older groups.
- **The shots tool.** The 12 PNGs for the report are taken from a match that has ticked (a scratch tool, not in the repository); the first set, from before the first tick, is kept beside them for comparison.

### Proof

- **Suite 3.18 `test_zoom_model`** (176,551 checks), **3.19 `test_zoom_view`** (4,587 checks, about 5 s) and **3.20 `test_zoom_fingerprint`** (138 fingerprints, 319 checks): see the README's table of suites. The first is pure numbers with every expectation written out independently of the header; the second drives the real renderer (the software renderer under SDL's dummy video driver), HUD and application and compares the pictures **with each other** (the target path with the direct path, the zoom 2 picture with the enlarged zoom 1 picture, the zoom 0.5 picture with its 2 x 2 average); the third pins the pictures and the pointer at 0.5 and 2 as 64-bit numbers, the way suite 3.10 pins the zoom 1. Every other suite is unchanged and passes, **including all 551 fingerprints of 3.10 (zoom 1 is exactly today's picture)**. The same hashes come out on macOS (clang, SDL 2.32.10) and Debian 12 (GCC 12.2, SDL 2.26.5).
- **A test hook for each path that cannot happen in the game**: `Renderer::set_force_world_target(true)` makes the zoom 1 pass go through the offscreen target (the equality test of the two paths), `set_fail_world_target(true)` makes the target impossible (the fall-back to the zoom 1 picture), and `world_target_passes()` counts the passes through the target (the zoom 1 never uses it; the forced and the zoomed pass use it once), so that a test that compares two paths can see that it really took the one it asks for (mutation RN29 found a comparison of the direct path with itself).
- **Mutations** (one change at a time in a scratch copy, the three suites rebuilt and run, the file restored; the older suites 3.10, 3.13, 3.15, 3.4, 3.5 and 3.2 are run for a mutation that none of the three kills): 159 mutations of the model, the camera, the renderer (the sprite pass included), the HUD, the edge scroll and the application (a zoom that does not anchor, a clamp that is not applied, a grid of the wrong size, a flipped wheel that is not undone, a limit that does not apply in a network match, a filter swapped, the digits drawn at the zoom's size, a copy that drops the half pixel, a zoom-out offered on a map that fits, a middle button that reaches the HUD, no ant drawn at a zoom ...). **154 are killed by the three new suites, 1 by the older suites only (ES06: the tiles form of the minimap scroll, used by 3.10, 3.13 and 3.4), and 4 are equivalent** (explained in the table: a map exactly as large as the view is 0 either way, the whole part of the camera's origin is always the floor of it, `set_zoom` asks again what `step_zoom` asked, and the start view scrolls from an origin that was set to 0 one line before). How the table came about: the first run (148 mutations) left 14 that no suite killed; ten of them got tests (the right click on an ant at a zoom, Ctrl+N and Ctrl+P at half-pixel origins and where the view has to move right and down, an anchor outside the view in x and in y, `zoom_levels()` in a match of the network, the level 1 under any limits, the fall-back of the world pass and a pass counter: RN29, a hook that made a "forced pass" compare the direct path with itself, was found that way), four are the equivalents, and five mutations were added for the new hooks. **Then a flaw of the runner showed: it read the exit code of `tail`, not of `cmake`, so a mutation that did not compile (an unused variable or parameter is an error under -Werror) was run against the binaries of the mutation before it, and was counted as killed or as a survivor by those (AP28 "survived" twice and was "killed" once for that reason).** The runner now stops at a build error; the whole set was run again, 11 mutations did not compile (VZ15, VZ18, VZ23, VZ24, CM08, RN10, RN20, AP12, ES02, ES04, RN36), they were rewritten to compile (an `(void)` for a variable that became unused, `true ||` for a condition that is bypassed) and run again. Last, the question about the ants (below) added six mutations of the sprite pass (RN40 - RN44, RN46), and **the whole set of 159 was run once more on the final tree: the table below is that run.** The numbers under "killed by" count the FAIL lines that a suite prints (at most 80 per suite).

| # | Mutation | Killed by (suite: failing checks) |
|---|---|---|
| M4-VZ01 | `view_zoom.hpp`: visible() rounds down | killed (3.18: 1, 3.19: 25, 3.20: 4) |
| M4-VZ02 | `view_zoom.hpp`: the grid is a world pixel at every zoom | killed (3.18: 60, 3.19: 57, 3.20: 8) |
| M4-VZ03 | `view_zoom.hpp`: snap rounds down | killed (3.18: 60) |
| M4-VZ04 | `view_zoom.hpp`: snap_toward_zero floors | killed (3.18: 1) |
| M4-VZ05 | `view_zoom.hpp`: world_at rounds up | killed (3.18: 60, 3.19: 80, 3.20: 12) |
| M4-VZ06 | `view_zoom.hpp`: the edge of a pixel rounds down | killed (3.18: 60, 3.19: 2, 3.20: 2) |
| M4-VZ07 | `view_zoom.hpp`: the lower limit is not enforced (a network match could zoom out) | killed (3.18: 56, 3.19: 11) |
| M4-VZ08 | `view_zoom.hpp`: the upper limit is not enforced | killed (3.18: 4) |
| M4-VZ09 | `view_zoom.hpp`: no zoom in is offered | killed (3.18: 60, 3.19: 80, 3.20: 12) |
| M4-VZ10 | `view_zoom.hpp`: a zoom-out is offered only when the map exceeds BOTH axes | killed (3.18: 5) |
| M4-VZ11 | `view_zoom.hpp`: the edge of the fit rule in x | killed (3.18: 1) |
| M4-VZ12 | `view_zoom.hpp`: the edge of the fit rule in y | killed (3.18: 1) |
| M4-VZ13 | `view_zoom.hpp`: the level above is the level itself | killed (3.18: 12, 3.19: 6, 3.20: 3) |
| M4-VZ14 | `view_zoom.hpp`: step goes the other way | killed (3.18: 60, 3.19: 57) |
| M4-VZ15 | `view_zoom.hpp`: step does not skip a level that is not offered | killed (3.18: 38) |
| M4-VZ16 | `view_zoom.hpp`: a current level that is no level counts as 0.5 | killed (3.18: 24) |
| M4-VZ17 | `view_zoom.hpp`: direction 0 steps out | killed (3.18: 24) |
| M4-VZ18 | `view_zoom.hpp`: a match starts at the remembered level whatever is offered | killed (3.18: 58, 3.19: 9) |
| M4-VZ19 | `view_zoom.hpp`: a map exactly as large as the view is clamped, not centred (equivalent: both give 0) | equivalent: a map exactly as large as the view gives 0 whether it is clamped to [0, 0] or centred |
| M4-VZ20 | `view_zoom.hpp`: a small map is centred even without the flag | killed (3.18: 60, 3.20: 1) |
| M4-VZ21 | `view_zoom.hpp`: the anchor ignores the old zoom | killed (3.18: 60, 3.19: 8) |
| M4-VZ22 | `view_zoom.hpp`: the zoomed origin is not put on the grid | killed (3.18: 60) |
| M4-VZ23 | `view_zoom.hpp`: the y anchor is the x anchor | killed (3.18: 60, 3.19: 10) |
| M4-VZ24 | `view_zoom.hpp`: natural scrolling is not undone | killed (3.18: 1, 3.19: 80) |
| M4-VZ25 | `view_zoom.hpp`: the whole amount beats the precise one | killed (3.18: 1) |
| M4-VZ26 | `view_zoom.hpp`: a change of direction keeps the fraction | killed (3.18: 2, 3.19: 1) |
| M4-VZ27 | `view_zoom.hpp`: a pause never forgets the fraction | killed (3.18: 1, 3.19: 18) |
| M4-VZ28 | `view_zoom.hpp`: the stale time counts one ms early | killed (3.18: 1) |
| M4-VZ29 | `view_zoom.hpp`: a step swallows the left-over fraction | killed (3.18: 10, 3.19: 4) |
| M4-VZ30 | `view_zoom.hpp`: the steps of one event are not bounded | killed (3.18: 1) |
| M4-VZ31 | `view_zoom.hpp`: a number that rounds to a level is a level | killed (3.18: 2) |
| M4-VZ32 | `view_zoom.hpp`: any character is accepted in a level (1e0, 0x1) | killed (3.18: 5, 3.19: 1) |
| M4-VZ33 | `view_zoom.hpp`: level_name of 0.5 | killed (3.18: 1, 3.20: 128) |
| M4-VZ35 | `view_zoom.hpp`: the level 1 follows the limits (equivalent: min is 1 at most) | killed (3.18: 1) |
| M4-CM01 | `renderer.cpp`: center_on centres by the screen size at a zoom (x) | killed (3.18: 8, 3.19: 14, 3.20: 8) |
| M4-CM02 | `renderer.cpp`: center_on centres by the screen size at a zoom (y) | killed (3.18: 8, 3.19: 9, 3.20: 8) |
| M4-CM03 | `renderer.cpp`: the camera clamp does not snap (x) | killed (3.18: 44, 3.19: 80) |
| M4-CM04 | `renderer.cpp`: the camera clamp does not snap (y) | killed (3.18: 60) |
| M4-CM05 | `renderer.cpp`: the camera clamp uses the wrong world size | killed (3.18: 60, 3.19: 80, 3.20: 70) |
| M4-CM06 | `renderer.cpp`: world_x of the zoomed clamp rounds | killed (3.18: 60, 3.19: 8) |
| M4-CM07 | `renderer.cpp`: the zoom 1 camera never centres a small map (x) | killed (3.18: 48, 3.19: 2) |
| M4-CM08 | `renderer.cpp`: set_zoom ignores the anchor | killed (3.18: 2, 3.19: 10) |
| M4-CM09 | `renderer.cpp`: world_to_screen does not scale (x) | killed (3.18: 60, 3.19: 2, 3.20: 4) |
| M4-CM10 | `renderer.cpp`: the inside test of world_to_screen ignores the zoom | killed (3.18: 1, 3.20: 1) |
| M4-CM11 | `renderer.cpp`: screen_to_world ignores the zoom (x) | killed (3.18: 60) |
| M4-CM12 | `renderer.hpp`: world_x_at ignores the zoom | killed (3.18: 60, 3.19: 80, 3.20: 28) |
| M4-CM13 | `renderer.hpp`: world_y_at ignores the zoom | killed (3.18: 60, 3.19: 80, 3.20: 30) |
| M4-CM14 | `renderer.hpp`: the edge of the band rounds down (x) | killed (3.18: 1, 3.19: 2, 3.20: 2) |
| M4-CM15 | `renderer.hpp`: the edge of the band ignores the zoom (y) | killed (3.19: 4, 3.20: 4) |
| M4-CM16 | `renderer.hpp`: the origin in screen pixels ignores the zoom | killed (3.18: 4, 3.19: 12, 3.20: 16) |
| M4-CM17 | `renderer.hpp`: the centre of the view ignores the zoom | killed (3.18: 2, 3.20: 4) |
| M4-CM18 | `renderer.hpp`: scroll_screen moves world pixels | killed (3.18: 20, 3.19: 16) |
| M4-CM19 | `renderer.hpp`: visible_w ignores the zoom | killed (3.18: 4) |
| M4-CM20 | `renderer.cpp`: set_zoom accepts any number | killed (3.18: 3) |
| M4-CM21 | `renderer.hpp`: the origin in screen pixels ignores the zoom (y) | killed (3.18: 4, 3.19: 12, 3.20: 24) |
| M4-CM22 | `renderer.hpp`: the centre of the view ignores the zoom (y) | killed (3.18: 1, 3.20: 4) |
| M4-RN01 | `renderer.cpp`: the target has no spare texel in x | killed (3.19: 17, 3.20: 4) |
| M4-RN02 | `renderer.cpp`: the target has no spare texel in y | killed (3.19: 16, 3.20: 4) |
| M4-RN03 | `renderer.cpp`: the target starts at the rounded origin (x) | killed (3.19: 22, 3.20: 11) |
| M4-RN04 | `renderer.cpp`: no half-pixel shift in x | killed (3.19: 17, 3.20: 5) |
| M4-RN05 | `renderer.cpp`: no half-pixel shift in y | killed (3.19: 16, 3.20: 5) |
| M4-RN06 | `renderer.cpp`: the target is not cleared | killed (3.19: 1) |
| M4-RN07 | `renderer.cpp`: the filters are swapped | killed (3.19: 46, 3.20: 62) |
| M4-RN08 | `renderer.cpp`: the crop does not skip the shift | killed (3.19: 25, 3.20: 6) |
| M4-RN09 | `renderer.cpp`: the copy is always one step | killed (3.19: 25, 3.20: 6) |
| M4-RN10 | `renderer.cpp`: the source is the whole target in x | killed (3.19: 11, 3.20: 30) |
| M4-RN11 | `renderer.cpp`: the copy ignores the shift in x | killed (3.19: 17, 3.20: 4) |
| M4-RN12 | `renderer.cpp`: the digits are drawn in the pass (they scale) | killed (3.19: 8, 3.20: 56) |
| M4-RN13 | `renderer.cpp`: the digits are deferred at the zoom 1 too | killed (3.19: 16) |
| M4-RN14 | `renderer.cpp`: a digit is placed without the zoom (x) | killed (3.19: 8, 3.20: 56) |
| M4-RN15 | `renderer.cpp`: a digit is placed without the zoom (y) | killed (3.19: 8, 3.20: 56) |
| M4-RN16 | `renderer.cpp`: the map rectangle ignores the zoom (width) | killed (3.19: 2) |
| M4-RN17 | `renderer.cpp`: the camera is not put back | killed (3.19: 77, 3.20: 56) |
| M4-RN18 | `renderer.cpp`: the picture is not put back | killed (3.19: 4) |
| M4-RN19 | `renderer.cpp`: the origin is not put back | killed (3.19: 2) |
| M4-RN20 | `renderer.cpp`: the zoom is not restored after a frame | killed (3.19: 16) |
| M4-RN21 | `renderer.cpp`: the zoom is kept after the pass has set it to 1 (the bug of the first screenshots) | killed (3.19: 47, 3.20: 16) |
| M4-RN22 | `renderer.hpp`: the world code reads the screen view in the pass | killed (3.19: 80, 3.20: 63) |
| M4-RN23 | `renderer.cpp`: the tile grid ignores the zoom | killed (3.19: 2) |
| M4-RN24 | `renderer.cpp`: render_map_layers ignores the zoom | killed (3.19: 1) |
| M4-RN25 | `renderer.cpp`: the tile grid is drawn in the pass too | killed (3.19: 2) |
| M4-RN26 | `renderer.cpp`: the pass camera keeps the zoom | killed (3.19: 49, 3.20: 65) |
| M4-RN27 | `renderer.cpp`: the pass camera keeps the screen view | killed (3.19: 80, 3.20: 62) |
| M4-RN28 | `renderer.cpp`: the pass draws with the picture and the origin of the screen | killed (3.19: 1) |
| M4-RN29 | `renderer.cpp`: the force hook does nothing in the world pass (a forced pass compares the direct path with itself) | killed (3.19: 16) |
| M4-RN30 | `renderer.cpp`: no fallback (equivalent while the target can be made) | killed (3.19: 8) |
| M4-RN31 | `renderer.cpp`: the copy has the width of the view | killed (3.19: 17, 3.20: 4) |
| M4-RN32 | `renderer.cpp`: the map rectangle ignores the zoom (left) | killed (3.19: 3) |
| M4-HD01 | `hud_input.cpp`: over_ground ignores the zoom | killed (3.19: 24, 3.20: 2) |
| M4-HD02 | `hud_input.cpp`: the cursor ignores the zoom | killed (3.19: 80, 3.20: 25) |
| M4-HD03 | `hud_input.cpp`: a click ignores the zoom | killed (3.19: 80, 3.20: 12) |
| M4-HD04 | `hud_input.cpp`: the band edge rounds down | killed (3.19: 2) |
| M4-HD05 | `hud_input.cpp`: the band start ignores the zoom | killed (3.19: 4) |
| M4-HD06 | `hud_input.cpp`: the right click on an ant ignores the zoom | killed (3.19: 80) |
| M4-HD07 | `hud_input.cpp`: the right click on ground ignores the zoom | killed (3.19: 80) |
| M4-HD08 | `hud.cpp`: the edge scroll ignores the zoom | killed (3.19: 14, 3.20: 14) |
| M4-HD09 | `hud.cpp`: the edge scroll uses the world origin at a zoom | killed (3.19: 14, 3.20: 31) |
| M4-HD10 | `hud.cpp`: the edge scroll uses the unscaled map height | killed (3.19: 8, 3.20: 16) |
| M4-HD11 | `hud.cpp`: the minimap uses the unscaled map width | killed (3.19: 4) |
| M4-HD12 | `hud.cpp`: the scroll moves world pixels | killed (3.19: 16) |
| M4-HD13 | `hud.cpp`: Ctrl+N uses the screen view as the world | killed (3.19: 12) |
| M4-HD14 | `hud.cpp`: Ctrl+N starts from the whole part at a zoom | equivalent: `world_x` is always `floor(x)` (every setter of the camera keeps them together), so the whole part is the same number |
| M4-HD15 | `hud.cpp`: the minimap frame ignores the zoom (width) | killed (3.19: 16, 3.20: 20) |
| M4-HD16 | `hud.cpp`: the minimap frame ignores the zoom (height) | killed (3.19: 18, 3.20: 20) |
| M4-HD17 | `hud.hpp`: the zoom ignores the dialogs | killed (3.19: 19) |
| M4-HD18 | `hud.hpp`: the zoom ignores a held press | killed (3.19: 18) |
| M4-HD19 | `hud.hpp`: the zoom ignores the chat log drag | killed (3.19: 1) |
| M4-HD20 | `hud.cpp`: the edge scroll uses the unscaled map width | killed (3.19: 8, 3.20: 25) |
| M4-HD21 | `hud.cpp`: Ctrl+N treats every zoom as the plain camera | killed (3.19: 8) |
| M4-AP01 | `application.hpp`: a network match offers the zoom-out | killed (3.19: 11) |
| M4-AP02 | `application.cpp`: a match does not take the remembered zoom | killed (3.19: 9) |
| M4-AP03 | `application.cpp`: the first match does not take the remembered zoom | killed (3.19: 7) |
| M4-AP04 | `application.cpp`: set_zoom does not check what is offered | killed (3.19: 5) |
| M4-AP05 | `application.cpp`: the anchor is not held to the view (x) | killed (3.19: 2) |
| M4-AP06 | `application.cpp`: the level is not remembered | killed (3.19: 3) |
| M4-AP07 | `application.cpp`: the level is not written to the settings | killed (3.19: 5) |
| M4-AP08 | `application.cpp`: the wheel acts outside a match | killed (3.19: 1) |
| M4-AP09 | `application.cpp`: the wheel acts over the results | killed (3.19: 2) |
| M4-AP10 | `application.cpp`: the wheel acts with the pointer outside the window | killed (3.19: 2) |
| M4-AP11 | `application.cpp`: the wheel ignores the HUD state | killed (3.19: 24) |
| M4-AP12 | `application.cpp`: the wheel acts over the panels | killed (3.19: 30) |
| M4-AP13 | `application.cpp`: a refused wheel event does not reset the accumulator | killed (3.19: 4) |
| M4-AP14 | `application.cpp`: natural scrolling is not honoured | killed (3.19: 80) |
| M4-AP15 | `application.cpp`: the wheel away zooms out | killed (3.19: 80) |
| M4-AP16 | `application.cpp`: the wheel toward zooms in | killed (3.19: 80) |
| M4-AP17 | `application.cpp`: the middle button also reaches the HUD | killed (3.19: 2) |
| M4-AP18 | `application.cpp`: the middle button ignores the rules of the wheel | killed (3.19: 10) |
| M4-AP19 | `application.cpp`: the middle button goes to 2 | killed (3.19: 21) |
| M4-AP20 | `application.cpp`: the wheel event is not handled | killed (3.19: 1) |
| M4-AP21 | `application.cpp`: the settings key is not read | killed (3.19: 7) |
| M4-AP22 | `application.cpp`: the key beats --zoom (and is ignored) | killed (3.19: 8) |
| M4-AP23 | `application.cpp`: the start view ignores the zoom | killed (3.19: 80, 3.20: 16) |
| M4-AP24 | `application.cpp`: the listener ignores the zoom (x) | killed (3.19: 80) |
| M4-AP25 | `application.cpp`: the listener ignores the zoom (y) | killed (3.19: 80) |
| M4-AP26 | `application.cpp`: --zoom is not an option | killed (3.19: 11) |
| M4-AP27 | `application.cpp`: step_zoom ignores the kind of match | equivalent: `set_zoom` asks `offered` again, and the three levels are neighbours with 1 always offered, so a step that the limits would have skipped is refused there |
| M4-AP28 | `application.cpp`: zoom_levels lists every level | killed (3.19: 2) |
| M4-AP29 | `application.cpp`: set_zoom of the current level says yes | killed (3.19: 6) |
| M4-AP30 | `application.cpp`: a match starts at the remembered level whatever the kind of match | killed (3.19: 9) |
| M4-AP31 | `application.cpp`: the start view moves world pixels from the old origin | equivalent: the line before sets the origin to 0, `scroll_pixels` adds to it, and the clamp snaps the origin to the zoom's grid as `set_origin` does |
| M4-AP32 | `application.cpp`: the anchor is not held to the view (y) | killed (3.19: 2) |
| M4-ES01 | `edge_scroll.hpp`: the tiles form of the edge scroll has half the map | killed (3.18: 3) |
| M4-ES02 | `edge_scroll.hpp`: the minimap x scale uses the height | killed (3.18: 2) |
| M4-ES03 | `edge_scroll.hpp`: the minimap square is a third of the view | killed (3.18: 6, 3.19: 6) |
| M4-ES04 | `edge_scroll.hpp`: the start view ignores the zoom | killed (3.18: 37, 3.19: 80, 3.20: 16) |
| M4-ES05 | `edge_scroll.hpp`: the plain start view is the zoom 0.5 one | killed (3.18: 16) |
| M4-ES06 | `edge_scroll.hpp`: the tiles form of the minimap scroll has half the height | killed by older suites only (3.10: 6, 3.13: 2, 3.4: 2) |
| M4-ES07 | `edge_scroll.hpp`: the east edge of the scroll is one pixel early | killed (3.18: 2) |
| M4-RN33 | `renderer.cpp`: the fail hook does nothing (the fall-back is never taken) | killed (3.19: 25) |
| M4-RN34 | `renderer.cpp`: render_map_layers has no fall-back | killed (3.19: 1) |
| M4-RN35 | `renderer.cpp`: the pass counter does not count | killed (3.19: 56) |
| M4-RN36 | `renderer.cpp`: render_map_layers leaves the fall-back camera at the zoom 1 | killed (3.19: 1) |
| M4-RN37 | `renderer.cpp`: the world pass has no fall-back (second look at RN30, with the test hook) | killed (3.19: 8) |
| M4-RN40 | `renderer.cpp`: no ant is drawn when the world goes through the offscreen target | killed (3.19: 80, 3.20: 70) |
| M4-RN41 | `renderer.cpp`: no ant is drawn at a zoom (the sprite function returns early) | killed (3.19: 53, 3.20: 62) |
| M4-RN42 | `renderer.cpp`: no ant is drawn at the zoom 0.5 | killed (3.19: 12, 3.20: 30) |
| M4-RN43 | `renderer.cpp`: no ant is drawn at the zoom 2 | killed (3.19: 41, 3.20: 32) |
| M4-RN44 | `renderer.cpp`: the y-sorted sprite queue (plants, ants, effects) is not drawn through the target | killed (3.19: 80, 3.20: 70) |
| M4-RN46 | `renderer.cpp`: the ants are culled by the screen view at a zoom (the sprite function) | killed (3.19: 8, 3.20: 26) |

- **Screenshots** (the `ants` binary, `--headless --map ... --screenshot ... --frames 12 --zoom Z --aspect ...`): GAUNTLET and TINY at 0.5, 1 and 2, in the 16:9 and the classic picture (12 PNGs, kept outside the repository): the hill is half the size / the size / twice the size of the original's, TINY at 0.5 is centred with black around it, the minimap's frame grows and shrinks with the zoom, the digits of Ctrl+L stay one size.
- **Timings** (a frame is `begin_frame`, `render_world`, `HUD::render` and a one pixel read-back that makes the GPU finish; GAUNTLET, 160 ants with hit point digits, effects and selection markers; 300 frames each, the scratch tool is not in the repository):

| Renderer and window | zoom 1 | zoom 0.5 | zoom 2 |
|---|---|---|---|
| software, wide, 960 x 540 window | 0.74 ms | 2.29 ms | 0.65 ms |
| software, wide, 1920 x 1080 window | 7.31 ms | 4.19 ms | 2.72 ms |
| Metal, wide, 1920 x 1080 points (3840 x 2160 pixels) | 1.40 ms | 1.44 ms | 1.12 ms |
| software, classic, 640 x 480 window | 0.47 ms | 1.30 ms | 0.42 ms |
| software, classic, 1280 x 960 window | 4.37 ms | 2.54 ms | 1.79 ms |
| Metal, classic | 1.17 ms | 1.08 ms | 0.82 ms |

  The accelerated renderer pays next to nothing for a zoom. The software renderer at 0.5 in a window the size of the canvas costs about 1.5 ms more than the zoom 1 frame (the target and one linear copy); in a window of twice the size, where SDL's software renderer scales every drawing call to the window at the zoom 1, the zoomed frames are cheaper than the zoom 1 frame, probably because the sprites go into a 1:1 target and only one scaled copy follows (not investigated). Metal at zoom 2, whole and half-pixel origin, equals the enlarged zoom 1 picture (0 of 6,096,000 window pixels differ).

### M4 on v0.0.99 (the zoom rebased onto the released 16:9 game)

The five commits of the zoom (written on 42ef62d) were rebased onto v0.0.99, which had rebased the same widescreen commits and added the wide setup screen with its map preview and the 16:9 web page. Textual conflicts: `application.hpp` (one comment line next to the two declarations of the zoom), `web/shell.html` (the guide's Scrolling list: v0.0.99's wording with the Zoom line), `run_tests.sh` and `tests/test_app/CMakeLists.txt` (the zoom's suites 3.18 - 3.20 after the wide setup (3.16) and the map preview (3.17)), `README.md`, `CHANGELOG.md` and this file (both sides kept). Everything else merged without a conflict; the places where the two pieces of work meet were read one by one:

- **The map preview (a real defect of the plain merge, now fixed).** `Renderer::render_world_image` (v0.0.99, the preview of the wide setup screen) sets its own window onto the world on the live camera and calls `render_world`. The renderer's camera keeps the zoom of the last match while the setup screen is up (the next match sets it again), so the first setup frame after a match at 0.5 or 2 drew the preview through the zoom's offscreen pass at a camera whose origin was not on the zoom's grid: a wrong picture, and it was cached for that map. Fixed in `renderer_world_image.cpp`: the preview's camera is set to the zoom 1 (the live camera, with its zoom, is put back with `saved_camera` as before) and the test hook that forces the offscreen target is cleared for the preview and put back. Everything else the preview restores was checked against the zoom's state: `in_world_target_` and `pass_` are idle between frames, `deferred_digits_` is empty at the zoom 1, `world_target_` and `scaled_target_` are not used by the preview's direct pass.
- **Tests of it.** `test_map_preview` `[render]`: at the live zoom 0.5 and 2 (and with the offscreen target forced) the image is the zoom 1 image byte for byte, drawn by the direct pass, and the live zoomed view, its camera and the frame it draws are as they were. `test_zoom_view` `[setup]`: through the application, a match at 0.5 and at 2 (TINY and GAUNTLET), back to the setup screen, the preview of two other maps in the list is the zoom 1 preview byte for byte; the wheel and the middle button over the preview, the map list, the old map view and the corner of the canvas do nothing on the setup screen (zoom, remembered level and origin unchanged), nor on the loading screen and the quick help. Mutation: without the line `camera_.zoom = zoom::kNormal` 8 checks of `[render]` and 4 of `[setup]` fail (the preview differs in 239,000 - 266,000 bytes of its 360,000); without clearing the forced target 4 of `[render]` fail (the forced pass is not even byte-equal: the tile's camera is not on the zoom's grid).
- **The network match and a single player** were already covered (`[net]`: a guest that remembered 0.5 starts at 1, 0.5 is refused by the wheel, `set_zoom` and `step_zoom`, a host and a guest zoom in and stay in the same state as a machine without a view; a local match afterwards starts at the remembered 0.5; `[fair]`: a local game and a game with bots offer 0.5); they pass on the merged tree unchanged.
- **The web page in a real browser** (Google Chrome headless, a throwaway profile, `--wheel` of `web_aspect_check.py`, run against `docker build -t ants-beta:zoomrc .`): in a window of 976 x 900 the canvas is exactly 960 x 540, so a canvas pixel is a screen pixel. A wheel (and a ctrl + wheel, which is a pinch in Chrome) over the title, the selector's bar and the guide scrolls the page and is not cancelled (it is not even cancelable there: the browser's own thread acts on it); over the canvas the page does not scroll and the event is cancelled. Safari's gesture events over the canvas are cancelled and become wheel events of 25 % a notch for the game; over the guide they are left alone. In a running match (Enter at the quick help, Enter at the setup screen, the start dialog gone) a wheel rolled away makes every world pixel of the map view a 2 x 2 square (99 % of the 2 x 2 blocks are one colour, 62 % at the zoom 1), two notches toward give the 0.5 picture, the frame around the view (its left strip) is the same picture at every level, the middle button goes back to 1 and a wheel over the minimap does not zoom; the same in the classic 4:3 picture (the view changes; its canvas pixel is 1.5 screen pixels there, so no squares are counted). Screenshots of the three levels are taken by the check (`--shots DIR`: `zoom_wide_05.png`, `zoom_wide_1.png`, `zoom_wide_2.png`, and the classic ones; not in the repository). Mutation of the served page: a canvas listener moved to `window` (the whole page cancelled) fails 6 checks, no `gesturechange` fails 1; removing the canvas's own `wheel` listener changes nothing in Chrome (an equivalent mutant: SDL's Emscripten handler is a non-passive listener on the same canvas and cancels the event itself, as the changelog said; the page's listener is for the browsers where it may not).
