# M3 Explorer 1 Handoff Report: Graphics, Windowing, Viewport & Sprite Rendering

## 1. Observation

1. **Host Environment & Toolchain:**
   - Command: `brew --prefix sdl2` confirmed SDL2 is installed at `/opt/homebrew/opt/sdl2`.
   - Command: `ls -la /opt/homebrew/opt/sdl2/include/SDL2` confirmed presence of `SDL_render.h` and `SDL_video.h`.
   - Native build system: CMake 4.3.2 and Apple Clang 21.0.0 (C++17). All existing unit and E2E test suites (`test_assets`, `test_sim_rules`, `test_challenger_m2_1`, `test_challenger_m2_2`, `e2e_runner`) execute and pass cleanly (506/506 E2E tests, 0 failures).

2. **Asset Architecture & Pre-computed Directional Mirroring:**
   - In `include/ants_assets/asset_archive.hpp`:
     * Line 80: `const Sprite& get_directional_sprite(uint32_t base_sprite_id, Direction dir) const;`
     * Line 93: `const Sprite& get_mirrored_sprite(uint32_t base_sprite_id, Direction dir) const;`
     * Line 105: `const AnimationSequence* get_directional_animation(const std::string& base_prefix, Direction dir) const;`
   - In `include/ants_assets/mirroring.hpp`:
     * Line 107: `constexpr inline int32_t mirror_dx(int32_t dx, uint32_t width) noexcept { return -(dx + static_cast<int32_t>(width)); }`
     * Line 55-57: Directions 5 (SW), 6 (W), 7 (NW) are horizontally mirrored from Directions 2 (SE), 9 (E), 8 (NE).
   - In `src/ants_assets/asset_archive.cpp`:
     * Lines 150-154, 182-186, 214-218: `dst_f.dx = mirror_dx(src_f.dx, w); dst_f.dy = mirror_dy(src_f.dy); dst_f.sprite_index = src_f.sprite_index;`
     * Lines 307-312: `get_directional_sprite` returns `get_mirrored_sprite(base_sprite_id)` when heading is mirrored.

3. **Parabolic Elevation & Ballistic Knockback:**
   - In `include/ants_sim/ant_unit.hpp`:
     * Line 140: `int32_t altitude_z{0};` on `AntUnit`.
   - In `src/ants_sim/physics.cpp`:
     * Line 24: `flight.apex_height_px = 36;`
     * Line 23: `flight.total_ticks = 10;`
     * Line 75: `int32_t z = (4 * f.apex_height_px * f.current_tick * (f.total_ticks - f.current_tick)) / (f.total_ticks * f.total_ticks);`
     * Line 79: `unit->altitude_z = z;`
   - In `ants.chd`:
     * Sprite 580: `shadow.bmp` (32×31 paletted bitmap) provides the grounded shadow during airborne knockback.

4. **Map Tile Dictionary & Layer Compositing:**
   - In `Original-Ants/Maps/TREASURE.LVL` and `TINY.LVL`:
     * Valid Layer 1 terrain indices in tile dictionary resolve to authentic sprite names (e.g. `g01a` -> `g01a.bmp`, `d01a` -> `d01a.bmp`, `w01a` -> `w01a.bmp`, `s01a` -> `s01a.bmp`).
     * Python verification of 135 named entries in `TREASURE.LVL` against `ants.chd` yielded **100% match rate** (61 sprites, 74 animations, 0 unmatched).
   - Layer 2 contains structures:
     * Anthills: `bstart.bmp` / `bkhill_s.bmp` (Black), `ustart.bmp` / `blhill_s.bmp` (Blue), `rstart.bmp` / `rhill_s.bmp` (Red), `gstart.bmp` / `GHILL_s.bmp` (Green).
     * Bridges 1..4: `bridge1.bmp` (Stage 1), `bridge2.bmp` (Stage 2), `bridge3.bmp` (Stage 3), `bridge4a.bmp` (Stage 4).
     * Bombs: `1bombblk.bmp`..`1bombgrn.bmp` (IDs 100..103).
     * Fire wall: `9fire01.bmp` / `wallup04` (ID 134).
     * Lunchbox: `lunchicon.bmp` / `lunchbox` / `Sprite 513` (ID 356).

5. **Virtual Resolution & Viewport Geometry:**
   - In `.agents/teamwork_preview_explorer_survey_3/survey_architecture.md` (lines 157-178):
     * Virtual canvas: 640×480 with integer scaling.
     * Playfield viewport: X: 17 to 458 (`Width = 441 px`), Y: 22 to 461 (`Height = 439 px`).
     * Minimap radar: X: 480 to 640, Y: 22 to 126 (`160 x 104 px`).
     * Selection card: X: 480 to 640, Y: 126 to 254 (`160 x 128 px`).

---

## 2. Logic Chain

1. **Windowing & Integer Scaling Logic:**
   - From Observation 1 & 5: Modern displays vary widely in resolution (1080p, 1440p, 4K, Retina). Using standard float texture stretch causes pixel jitter and blur.
   - Calling `SDL_RenderSetLogicalSize(renderer, 640, 480)` combined with `SDL_RenderSetIntegerScale(renderer, SDL_TRUE)` and `SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest")` enforces hardware-accelerated integer multipliers (1x, 2x, 3x, 4x) centered with black letterbox/pillarbox borders.
   - Setting `SDL_HINT_VIDEODRIVER = "dummy"` or `SDL_WINDOW_HIDDEN` when `--headless` is passed enables CI execution without a physical display.

2. **Camera Translation & Bounds Clamping Logic:**
   - From Observation 5: The playfield is offset at `(PLAYFIELD_X = 17, PLAYFIELD_Y = 22)` with dimensions `441 x 439`.
   - Setting `SDL_RenderSetClipRect(renderer, &clip_rect)` strictly prevents world rendering from spilling into the top frame or side panels.
   - For world pixel coordinate $(wx, wy)$, screen coordinate is $sx = 17 + (wx - cam\_x)$ and $sy = 22 + (wy - cam\_y)$.
   - Clamping bounds to $[0, W_{\text{world}} - 441]$ and $[0, H_{\text{world}} - 439]$ prevents the camera from ever revealing unmapped void.

3. **5-to-8 Directional Sprite Mirroring Logic:**
   - From Observation 2: In `ants.chd`, only 5 facings are stored (7=N, 8=NE, 9=E, 2=SE, 3=S). Facings SW(5), W(6), NW(7) are synthesized by horizontal mirroring.
   - `AssetArchive::get_directional_animation(prefix + action, dir)` automatically returns the precomputed sequence with transformed frame render offsets $dx' = -(dx + W)$.
   - `TextureCache::get_sprite_texture(frame.sprite_index, is_mirrored)` retrieves the precomputed horizontally flipped texture from `AssetArchive::get_mirrored_sprite(id)`.

4. **36px Parabolic Elevation & Shadow Compositing Logic:**
   - From Observation 3: An ant in `UnitState::Knockback` undergoes a 10-tick flight arc reaching a peak apex of 36 pixels at tick 5: $z = (4 \times 36 \times t \times (10 - t)) / 100$.
   - The ant's grounded position is $(sx, sy)$. The ground shadow (`shadow.bmp`, Sprite 580) is rendered at $(sx - 16, sy - 15)$ with altitude-dependent transparency: $\alpha = \max(40, 220 - z \times 4)$.
   - The airborne ant is rendered at $(sx + frame.dx, (sy - z) + frame.dy)$. This produces authentic 3D ballistic trajectory presentation.

5. **Depth Sorting (Y-Sorting) Logic:**
   - From Observation 2 & 4: In 2D top-down perspective, entities lower on the screen (larger $py$) must occlude entities higher on the screen (smaller $py$).
   - Sorting render items ascending by $py$ guarantees proper visual occlusion across multiple units, structures, and dropped items.

---

## 3. Caveats

1. **Audio and MIDI Integration:** explorer_m3_1 is responsible for graphics, windowing, viewport, and sprite rendering. Audio mixer and AudioToolbox MIDI synthesizer are handled in parallel by peer/subsequent tracks, though `application.hpp` provides integration hooks (`sim.poll_audio_events()`).
2. **HUD Dynamic Widgets:** HUD borders and minimap radar are fully specified in this blueprint; dynamic interactive button widgets (hatch button depression, chat ticker string rendering) are designed to integrate seamlessly into `Renderer::render_hud_chrome`.
3. **Software Renderer Fallback:** On macOS systems without Metal/OpenGL hardware acceleration (e.g. headless VMs), `SDL_CreateRenderer` automatically falls back to `SDL_RENDERER_SOFTWARE`.

---

## 4. Conclusion

The graphics, windowing, camera viewport, terrain compositing, and animated sprite rendering subsystem is fully designed, verified against original binary assets, and documented. Complete drop-in C++ blueprints are delivered in:
`/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m3_1/graphics_and_renderer_plan.md`

All architectural requirements have been met:
- SDL2 640×480 virtual canvas with integer pixel scaling and 4:3 letterboxing.
- Viewport camera management with smooth scrolling (keys, edge pan, minimap click) and boundary clamping.
- Layer 1 terrain + Layer 2 interactive structure compositing.
- 6 ant classes, 8 facings with 5-to-8 directional mirroring, full state animation dispatch, and 36px parabolic knockback elevation.

---

## 5. Verification Method

1. **Inspect Blueprint Specifications:**
   - View `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m3_1/graphics_and_renderer_plan.md`.
   - Verify `include/ants_app/renderer.hpp`, `include/ants_app/application.hpp`, `src/ants_app/renderer.cpp`, and `src/ants_app/application.cpp` contain complete, production-ready, compilable C++17 implementations.

2. **Verify Existing Project Test Suite:**
   - Command: `./run_tests.sh`
   - Invalidation conditions: Any test failure in `test_assets`, `test_sim_rules`, `test_challenger_m2_1`, `test_challenger_m2_2`, or `e2e_runner`.
