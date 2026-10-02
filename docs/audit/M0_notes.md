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

