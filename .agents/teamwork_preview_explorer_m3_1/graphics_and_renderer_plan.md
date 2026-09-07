# Ants Remake — Graphics, Windowing, Viewport & Sprite Rendering Architecture
**Document Version:** 1.0  
**Author:** explorer_m3_1 (M3 Explorer 1: Graphics & Renderer Architecture)  
**Target Milestone:** Milestone 3 (`ants-app`)  
**Host Environment:** macOS 15.x / Darwin 25.6.0 (Apple Silicon arm64, Apple Clang C++17, SDL2 2.32.10)

---

## 1. Executive Summary

This specification defines the complete graphics, windowing, camera viewport, terrain compositing, and animated sprite rendering subsystem for `ants-app`. 

`ants-app` serves as the interactive desktop presentation client of the Ants remake. It sits cleanly on top of `libants-assets` (asset decompression and format decoding) and `libants-sim` (deterministic 20 Hz simulation engine).

### Key Architectural Pillars
1. **Authentic 4:3 640×480 Virtual Canvas:**
   - Hardware-accelerated SDL2 2D rendering pipeline.
   - Logical resolution locked to 640×480 with nearest-neighbor integer scaling (`SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest")`) and aspect-ratio preserving pillarboxing / letterboxing (`SDL_RenderSetLogicalSize(renderer, 640, 480)`).
   - Headless execution support (`--headless` or `SDL_HINT_VIDEODRIVER = "dummy"`) enabling 100% automated CI testing without physical display attachments.
2. **Smooth Viewport Camera & World Coordinate Translation:**
   - Playfield bounds: X: 17 to 458 (`Width = 441 px`), Y: 22 to 461 (`Height = 439 px`).
   - Hard clipping rectangle (`SDL_RenderSetClipRect`) isolating the playfield from top bar, bottom banner, and right-hand HUD chrome.
   - 4-way navigation: Keyboard (Arrow keys / WASD), mouse edge-panning (8px perimeter), middle-mouse drag, and instant minimap radar click-to-center.
   - Mathematical bounds clamping preventing viewport overscroll past map tile boundaries.
3. **Multi-Layer Terrain Compositing:**
   - **Layer 1 Base Terrain:** 32×32 pixel tiles directly resolved from `LevelData::tile_dictionary` names (e.g. `g01a` grass, `d01a` dirt, `w01a` water, `s01a` sand, transitions) mapped to paletted sprites in `ants.chd`.
   - **Layer 2 Interactive Objects & Structures:** Anthills (`BSTART`..`GSTART`, `BLACKHILL`..`GREENHILL`), bridges at stages 1–4 (`bridge1`..`bridge4`), bombs in team colors (`1bombblk`..`1bombgrn`), fire walls (`wallup04`), food items (`food.bmp`, `fdcola1`, etc.), power-up tiles (`pu_comb`..`pu_mason`), and dropped lunchbox (`Sprite 513` / `lunchicon.bmp`).
4. **Authentic Animated Ant Sprite Pipeline:**
   - 6 ant classes: Worker (`ag`), Bomber (`ab`), Fire (`af`), Thief (`at`), Combat (`ac`), Swimmer (`as`).
   - 8-directional facings powered by `libants-assets` 5-to-8 directional mirroring (`get_directional_sprite` and `get_mirrored_sprite`). Western compass headings (SW=5, W=6, NW=7) use precomputed mirrored bitmaps and transformed frame render offsets `dx' = -(dx + W)`.
   - Complete state animations: Idle (`*st`), Walk (`*wg` / `*ws` carrying), Attack (`*at`), Bomb plant (`absb`) / squash defuse (`abdb`), Fire ignite (`afsf`) / extinguish (`afxf`), Bridge build (`asdb` / `asbb`), Base entry/emergence (`hgen301`), Thief infiltration (`atcr501`), Drowning (`*dr301` 22-subitem sequence + splash `dsplash`).
   - 36px apex parabolic elevation offset rendering during Combat Ant knockback flight: vertical offset `-altitude_z`, while `shadow.bmp` (Sprite 580, 32×31) remains grounded with distance-based alpha.

---

## 2. Windowing, Virtual Resolution & 4:3 Aspect Scaling Architecture

### 2.1 Virtual Canvas Geometry
The original 1995/1998 Ants ran exclusively at 640×480 with a 256-color palette. To preserve authentic presentation across modern displays (1080p, 1440p, 4K, 5K Retina) without blurriness, the client implements fixed logical integer scaling:

```
0,0 ───────────────────────────────────────────────────────────── 640,0
│ [Top Bar: x0y0.bmp (640x22)] Title, Clock (mm:ss), Scores      │
├──────────────────────────────────────────┬─────────────────────┤
│                                          │ [Minimap / Radar]   │
│                                          │  (480, 22) - (640, 126)
│                                          │  160 x 104 px       │
│                                          ├─────────────────────┤
│                                          │ [Selection Card]    │
│       PLAYFIELD VIEWPORT                 │  (480, 126)-(640, 254)
│       (X: 17 to 458, Y: 22 to 461)       │  160 x 128 px       │
│       Dimensions: 441 x 439 px           ├─────────────────────┤
│                                          │ [Hatch & Egg Pile]  │
│                                          │  (480, 254)-(640, 360)
│                                          ├─────────────────────┤
│                                          │ [Action Buttons]    │
│                                          │  (480, 360)-(640, 461)
├──────────────────────────────────────────┴─────────────────────┤
│ [Bottom Bar: x17y461.bmp (623x19)] News Flash Banner / Chat    │
0,480 ─────────────────────────────────────────────────────────── 640,480
```

### 2.2 SDL2 Initialization & Scaling Pipeline
```cpp
// 1. Force crisp nearest-neighbor texture filtering
SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");

// 2. High-DPI and resizable window creation
uint32_t window_flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
if (headless_mode) {
    window_flags = SDL_WINDOW_HIDDEN;
    SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
}

SDL_Window* window = SDL_CreateWindow(
    "Ants",
    SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
    1280, 960, // Default 2x integer scale
    window_flags
);

// 3. Hardware-accelerated VSync renderer
SDL_Renderer* renderer = SDL_CreateRenderer(
    window, -1,
    SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
);

// 4. Set 640x480 logical virtual resolution with authentic 4:3 letterboxing
SDL_RenderSetLogicalSize(renderer, 640, 480);
SDL_RenderSetIntegerScale(renderer, SDL_TRUE); // Locks integer multiplier, centers with black borders
```

### 2.3 Headless / CI Mode
For automated regression testing and CI without an active window server or display:
- When launched with `--headless`, the application sets `SDL_HINT_VIDEODRIVER = "dummy"` or uses `SDL_WINDOW_HIDDEN`.
- `SDL_CreateRenderer` creates a software or off-screen context.
- The game loop executes simulation ticks, updates HUD states, and renders frames into off-screen memory before cleanly exiting with code 0.

---

## 3. Viewport Camera Management & Math

### 3.1 Playfield Clipping & World Translation
The playfield is bounded inside the 640×480 screen at:
- `PLAYFIELD_X = 17`
- `PLAYFIELD_Y = 22`
- `PLAYFIELD_W = 441`
- `PLAYFIELD_H = 439`

When drawing game entities (terrain, bridges, items, ants, particles), the renderer sets:
```cpp
SDL_Rect playfield_clip = { 17, 22, 441, 439 };
SDL_RenderSetClipRect(renderer, &playfield_clip);
```
After world rendering finishes, `SDL_RenderSetClipRect(renderer, nullptr)` is called to allow the HUD chrome and modal overlays to render across full screen bounds.

### 3.2 Coordinate Conversion Equations
Let `(cam_x, cam_y)` be the world pixel coordinate of the top-left corner of the camera viewport.

1. **World Pixels to Screen Pixels:**
   $$\text{screen\_x} = 17 + (\text{world\_px} - \text{cam\_x})$$
   $$\text{screen\_y} = 22 + (\text{world\_py} - \text{cam\_y})$$

2. **Screen Pixels to World Pixels:**
   $$\text{world\_px} = \text{cam\_x} + (\text{screen\_x} - 17)$$
   $$\text{world\_py} = \text{cam\_y} + (\text{screen\_y} - 22)$$

3. **Screen Pixels to Grid Tile Coordinates (32×32 tiles):**
   $$\text{tile\_x} = \lfloor \text{world\_px} / 32 \rfloor$$
   $$\text{tile\_y} = \lfloor \text{world\_py} / 32 \rfloor$$

### 3.3 Camera Bounds Clamping
A map with grid dimensions $(W_{\text{grid}}, H_{\text{grid}})$ has total world pixel dimensions:
$$W_{\text{world}} = W_{\text{grid}} \times 32, \quad H_{\text{world}} = H_{\text{grid}} \times 32$$

The camera top-left must never scroll past the map perimeter:
$$\text{cam\_x} = \max(0, \min(\text{cam\_x}, W_{\text{world}} - 441))$$
$$\text{cam\_y} = \max(0, \min(\text{cam\_y}, H_{\text{world}} - 439))$$

### 3.4 Camera Navigation Controls
1. **Keyboard Scrolling:**
   - Arrow keys / WASD keys pan camera at a constant speed of 480 pixels/second ($\Delta t \times 480$).
2. **Mouse Edge Panning:**
   - When cursor is within 8 pixels of playfield boundary (or screen edges):
     * $x \le 25 \implies$ pan West
     * $x \ge 450 \implies$ pan East
     * $y \le 30 \implies$ pan North
     * $y \ge 453 \implies$ pan South
3. **Minimap Click-to-Center:**
   - Clicking coordinate $(mx, my)$ within radar bounds (X: 480..640, Y: 22..126, Radar Size: $160 \times 104$):
     $$\text{world\_click\_x} = \frac{mx - 480}{160} \times W_{\text{world}}$$
     $$\text{world\_click\_y} = \frac{my - 22}{104} \times H_{\text{world}}$$
     $$\text{cam\_x} = \text{world\_click\_x} - \frac{441}{2}, \quad \text{cam\_y} = \text{world\_click\_y} - \frac{439}{2}$$
     Clamped to world bounds immediately.
4. **Unit Centering:**
   - Spacebar or double-clicking an ant centers $(cam\_x, cam\_y)$ on the selected unit's anchor $(px, py)$.

---

## 4. Texture Management & 256-Color Palette Pipeline

### 4.1 Master 256-Color Palette & Transparency Key
`ants.chd` contains the master 256-color palette (1024 bytes, 256 `ColorRGBA` entries):
- Color index `254` (`0xFE`, pure Magenta `RGB(255, 0, 255)`) is the designated transparency color key: `Alpha = 0`.
- All other palette indices have `Alpha = 255`.

### 4.2 Lazy Texture Caching (`TextureCache`)
With 2,794 original sprites + 2,794 pre-mirrored sprites:
- Total raw pixel data across all sprites is $\approx 6.8\text{ MB}$.
- An on-demand `TextureCache` stores `SDL_Texture*` pointers indexed by `(sprite_id, mirrored_flag)`.
- When a sprite is requested for the first time:
  1. Retrieve `ants::assets::Sprite` from `AssetArchive::get_sprite` or `get_mirrored_sprite`.
  2. Convert 8-bit paletted pixels to 32-bit tightly-packed RGBA buffer via `sp.to_rgba32(palette)`.
  3. Create `SDL_Surface*` via `SDL_CreateRGBSurfaceWithFormatFrom(rgba.data(), w, h, 32, w * 4, SDL_PIXELFORMAT_RGBA32)`.
  4. Create `SDL_Texture*` via `SDL_CreateTextureFromSurface(renderer, surface)`.
  5. Set blend mode `SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND)`.
  6. Store texture in cache; free surface.
- On match termination or map switch, all allocated `SDL_Texture*` instances are released via `SDL_DestroyTexture`.

---

## 5. Terrain Rendering & Compositing Engine

### 5.1 Layer 1: Base Terrain Grid
For every tile column $c \in [\text{start\_col}, \text{end\_col}]$ and row $r \in [\text{start\_row}, \text{end\_row}]$:
1. Lookup cell in `Grid::get_cell(c, r)`:
   - `terrain_id`: Dictionary index in `LevelData::tile_dictionary`.
2. Dictionary Name Resolution:
   - `tile_dictionary[terrain_id]` yields the ASCII asset name (e.g. `"g01a"`, `"d01a"`, `"w01a"`, `"m01a"`, etc.).
   - Asset lookup tests both `name` and `name + ".bmp"`.
   - If resolved, returns `sprite_id`.
   - Fallback: Default category color or base terrain sprite if index is placeholder (`"."`).
3. Render 32×32 quad at:
   $$\text{dest\_x} = 17 + (c \times 32 - \text{cam\_x})$$
   $$\text{dest\_y} = 22 + (r \times 32 - \text{cam\_y})$$
   `SDL_RenderCopy(renderer, tile_texture, nullptr, &dest_rect);`

### 5.2 Layer 2: Interactive Objects & Structures
Composited directly on top of Layer 1:

| Entity | Simulation State | Visual Asset in `ants.chd` | Rendering Behavior |
|---|---|---|---|
| **Anthill (Base)** | `AnthillSpawn` / `cell.interactive_id` | Team 0: `bstart.bmp` / `bkhill_s.bmp`<br/>Team 1: `ustart.bmp` / `blhill_s.bmp`<br/>Team 2: `rstart.bmp` / `rhill_s.bmp`<br/>Team 3: `gstart.bmp` / `GHILL_s.bmp` | Drawn at $(x \times 32, y \times 32)$. Center hole coordinates serve as ant entry/emergence anchor. |
| **Bridges (Stages 1..4)** | `interactive_id`: 34..37 (`TILE_BRIDGE1`..`4`) | Stage 1: `bridge1.bmp`<br/>Stage 2: `bridge2.bmp`<br/>Stage 3: `bridge3.bmp`<br/>Stage 4: `bridge4a.bmp` (Complete) | Drawn over water cell. When stage = 4, renders complete wooden plank bridge. |
| **Bombs** | `interactive_id`: 100..103 (`BOMB_BLACK`..`GREEN`) | Team 0: `1bombblk.bmp`<br/>Team 1: `1bombblu.bmp`<br/>Team 2: `1bombred.bmp`<br/>Team 3: `1bombgrn.bmp` | Pulsing animation loop (`redbomb`, `greenbomb`, etc., or 2-frame alternate). |
| **Fire Wall** | `interactive_id`: 134 (`TILE_FIREWALL`) | `wallup04` animation sequence (`9fire01.bmp`..`9fire04.bmp`) | Multi-frame roaring flame loop. Drawn with additive or alpha blend. |
| **Food Items** | `cell.has_food()` | `food.bmp`, `fdcola1.bmp`, `fdgumw1.bmp`, `fdburgr.bmp`, `fdsuckr.bmp`, etc. | Drawn centered in 32×32 tile. |
| **Power-Up Items** | `interactive_id`: 62..66 | `pu_comb.bmp`, `pu_thief.bmp`, `pu_bomb.bmp`, `pu_swim.bmp`, `pu_mason.bmp` | Glowing powerup crystal/icon. |
| **Dropped Lunchbox** | `interactive_id`: 356 (`TILE_LUNCHBOX`) | `lunchbox` animation / `Sprite 513` / `lunchicon.bmp` | Dropped upon food carrier death; available for universal pickup. |

---

## 6. Animated Ant Sprite Rendering Pipeline

### 6.1 Ant Classes, Facings & Mirroring
Ants contains 6 playable ant classes:
- `Worker` (`ag`)
- `Bomber` (`ab`)
- `Fire` (`af`)
- `Thief` (`at`)
- `Combat` (`ac`)
- `Swimmer` (`as`)

Each ant has 8 compass facings: North (0), NorthEast (1), East (2), SouthEast (3), South (4), SouthWest (5), West (6), NorthWest (7).
In `ants.chd`, only 5 directional facings are stored:
- `7` = North (0)
- `8` = NorthEast (1)
- `9` = East (2)
- `2` = SouthEast (3)
- `3` = South (4)

Western headings are obtained via horizontal reflection:
- SouthWest (5) $\leftarrow$ Mirrored SouthEast (2)
- West (6) $\leftarrow$ Mirrored East (9)
- NorthWest (7) $\leftarrow$ Mirrored NorthEast (8)

### 6.2 Animation State Dispatch Table

| Unit State | Sub-Condition | Animation Action Prefix | Notes |
|---|---|---|---|
| `Idle`, `GuardIdle`, `Stunned` | Land | `<class>st` (e.g. `agst`, `acst`) | Standing ready; loops 4–8 frames. |
| `Idle` | Swimming | `assw` (Swimmer only) | Aquatic treading water idle. |
| `Walking`, `Intercepting`, `ReturningToPost` | Not carrying | `<class>wg` (e.g. `agwg`, `afwg`) | Standard 8-directional walk cycle. |
| `Walking` | Carrying food / lunchbox | `<class>ws` (e.g. `agws`, `afws`) | Walking while holding item aloft. |
| `Walking` | Swimming in water | `assw` (Swimmer only) | Aquatic swimming stroke cycle. |
| `Attacking` | Melee strike | `<class>at` (e.g. `agat`, `acat`) | Standard 1 HP hit or Combat 2 HP punch. |
| `Ability` | Bomber Ant plant | `absb301` | Arming and placing landmine (Sound 90). |
| `Ability` | Bomber Ant defuse | `abdb301` | Body crush squash onto enemy mine (Sounds 73+74). |
| `Ability` | Fire Ant ignite | `afsf301` | Magnifying glass sunbeam flame ignition (Sounds 67+68). |
| `Ability` | Fire Ant extinguish | `afxf301` | Sputter extinguish smother (Sound 69). |
| `Ability` | Swimmer Ant bridge | `asdbl301` / `asdbw701` | Shoveling bridge gravel (Sounds 81+82). |
| `Ability` | Thief Ant dive | `atcr501` | 33-frame stealth infiltration dive into enemy anthill. |
| `EnteringBase` | Base entry / heal | `hgen301` / `agen301` | 17-frame base descent, food deposit, underground heal. |
| `Flinch` | Hit reaction | `<class>gh` (e.g. `aggh`) | Brief recoil flinch. |
| `Knockback` | Airborne flight | `<class>gf` (e.g. `aggf`, `acgf`) | Ballistic flailing flight in the air. |
| `Bounce` | Ground impact skid | `<class>gb` (e.g. `aggb`) | Landing impact roll and recovery. |
| `Drowning` | Deep water submersion | `<class>dr301` (e.g. `agdr301`) | 22-subitem drowning sequence + `dsplash` plume. |

### 6.3 36px Apex Parabolic Elevation Rendering (Knockback Altitude)
When an ant is struck by Combat Ant Heavy Punch or Bomb Detonation:
- `flight.apex_height_px = 36`
- `flight.total_ticks = 10`
- At simulation tick $t \in [0, 10]$:
  $$\text{altitude\_z} = \frac{4 \times 36 \times t \times (10 - t)}{100}$$
  * $t = 0 \implies z = 0$
  * $t = 5 \implies z = \frac{4 \times 36 \times 25}{100} = 36\text{ px}$ (Peak apex)
  * $t = 10 \implies z = 0$ (Ground landing)

#### Render Compositing Math:
1. **Ground Shadow:**
   - Ground position: $\text{sx} = 17 + (px - cam\_x)$, $\text{sy} = 22 + (py - cam\_y)$.
   - Draw `Sprite 580: shadow.bmp` (32×31) centered at ground:
     $$\text{shadow\_x} = \text{sx} - 16, \quad \text{shadow\_y} = \text{sy} - 15$$
   - Alpha modulation: $\alpha = \max(40, 220 - \text{altitude\_z} \times 4)$.
2. **Elevated Ant Sprite:**
   - Visual ant vertical translation:
     $$\text{draw\_x} = \text{sx} + \text{frame.dx}$$
     $$\text{draw\_y} = (\text{sy} - \text{altitude\_z}) + \text{frame.dy}$$
   - Draw directional ant sprite with `dx' = -(dx + W)` if mirrored.
3. This creates a realistic top-down 3D ballistic arc, with the ant soaring 36 pixels high above its grounded shadow before slamming back to earth.

### 6.4 Depth Sorting (Y-Sorting)
To ensure correct visual occlusion when units walk in front of or behind structures and other units:
- Every active ant entity is queued into a render list with `sort_key = py`.
- Entities are sorted ascending by `sort_key`:
  1. Ground shadows (rendered first)
  2. Ground structures / items (anthills, food, bombs)
  3. Ants with smaller $py$ (further North / background)
  4. Ants with larger $py$ (further South / foreground)
  5. Airborne particles, water splash plumes (`dsplash`), and floating text
  6. Selection circles and health bars

---

## 7. Public C++ Headers and Implementation Blueprints

The following four blueprints provide complete drop-in implementations for `ants-app`:

### 7.1 Blueprint 1: `include/ants_app/renderer.hpp`
```cpp
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <memory>
#include <unordered_map>
#include <functional>

#include <SDL.h>

#include "ants_assets/asset_archive.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_sim/sim_engine.hpp"

namespace ants::app {

// Authentic Virtual Canvas Constants
constexpr int CANVAS_WIDTH  = 640;
constexpr int CANVAS_HEIGHT = 480;

// Authentic Playfield Viewport Bounds
constexpr int PLAYFIELD_X = 17;
constexpr int PLAYFIELD_Y = 22;
constexpr int PLAYFIELD_W = 441;
constexpr int PLAYFIELD_H = 439;

// Authentic HUD Subsystem Rectangles
constexpr int MINIMAP_X = 480;
constexpr int MINIMAP_Y = 22;
constexpr int MINIMAP_W = 160;
constexpr int MINIMAP_H = 104;

constexpr int CARD_X = 480;
constexpr int CARD_Y = 126;
constexpr int CARD_W = 160;
constexpr int CARD_H = 128;

constexpr int TILE_SIZE = 32;

/**
 * @brief Viewport Camera tracking world position with smooth scrolling and clamping.
 */
struct ViewportCamera {
    float x{0.0f}; // Top-left world X
    float y{0.0f}; // Top-left world Y
    float target_x{0.0f};
    float target_y{0.0f};
    float scroll_speed{480.0f}; // Pixels per second

    void pan(float dx, float dy, float dt, uint32_t map_w, uint32_t map_h);
    void center_on(int32_t world_px, int32_t world_py, uint32_t map_w, uint32_t map_h);
    void clamp_to_bounds(uint32_t map_w, uint32_t map_h);

    bool world_to_screen(int32_t wx, int32_t wy, int32_t& sx, int32_t& sy) const noexcept;
    bool screen_to_world(int32_t sx, int32_t sy, int32_t& wx, int32_t& wy) const noexcept;
    bool is_tile_visible(int32_t tx, int32_t ty) const noexcept;
};

/**
 * @brief Texture cache converting raw paletted CHD sprites to hardware SDL_Textures.
 */
class TextureCache {
public:
    TextureCache(SDL_Renderer* renderer, const ants::assets::AssetArchive& archive);
    ~TextureCache();

    TextureCache(const TextureCache&) = delete;
    TextureCache& operator=(const TextureCache&) = delete;

    SDL_Texture* get_sprite_texture(uint32_t sprite_id, bool mirrored = false);
    SDL_Texture* get_named_sprite_texture(const std::string& name, bool mirrored = false);
    void clear();

private:
    SDL_Renderer* renderer_{nullptr};
    const ants::assets::AssetArchive& archive_;
    std::unordered_map<uint64_t, SDL_Texture*> textures_; // Key: (sprite_id << 1) | (mirrored ? 1 : 0)
};

/**
 * @brief Render item representation for depth-sorted playfield drawing.
 */
struct RenderItem {
    int32_t sort_y{0};
    std::function<void(SDL_Renderer*, TextureCache&)> draw_func;
};

/**
 * @brief Hardware-accelerated 2D Renderer managing canvas, viewport, terrain, and sprites.
 */
class Renderer {
public:
    Renderer();
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool init(SDL_Window* window,
              const ants::assets::AssetArchive& archive,
              bool integer_scale = true);
    void shutdown();

    void set_level(const ants::assets::LevelData& level);

    // Frame Lifecycle
    void begin_frame();
    void render_world(const ants::sim::WorldState& world,
                      const ants::sim::Grid& grid,
                      int32_t selected_unit_id);
    void render_minimap(const ants::sim::WorldState& world,
                        const ants::sim::Grid& grid);
    void render_hud_chrome(const ants::sim::WorldState& world,
                           int32_t selected_unit_id);
    void end_frame();

    // Camera Accessors
    ViewportCamera& camera() noexcept { return camera_; }
    const ViewportCamera& camera() const noexcept { return camera_; }

    // Helpers
    SDL_Renderer* get_sdl_renderer() const noexcept { return renderer_; }
    TextureCache* get_texture_cache() const noexcept { return texture_cache_.get(); }

private:
    void render_terrain_layer1(const ants::sim::Grid& grid);
    void render_terrain_layer2_structures(const ants::sim::Grid& grid);
    void render_ant_units(const ants::sim::WorldState& world, int32_t selected_unit_id);
    void draw_ant_shadow(int32_t anchor_sx, int32_t anchor_sy, int32_t altitude_z);
    void draw_single_ant(const ants::sim::AntSnapshot& ant, bool is_selected);

    SDL_Renderer* renderer_{nullptr};
    const ants::assets::AssetArchive* archive_{nullptr};
    std::unique_ptr<TextureCache> texture_cache_;
    ViewportCamera camera_;

    uint32_t map_width_{0};
    uint32_t map_height_{0};
    std::vector<int32_t> tile_sprite_ids_; // Pre-resolved tile dictionary to sprite ID cache
    std::vector<RenderItem> render_queue_;
};

} // namespace ants::app
```

---

### 7.2 Blueprint 2: `include/ants_app/application.hpp`
```cpp
#pragma once

#include <cstdint>
#include <string>
#include <memory>
#include <vector>

#include <SDL.h>

#include "ants_assets/asset_archive.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_app/renderer.hpp"

namespace ants::app {

struct ApplicationConfig {
    std::string title{"Ants"};
    int window_width{1280};  // Default 2x integer scale
    int window_height{960};
    bool fullscreen{false};
    bool integer_scaling{true};
    bool vsync{true};
    bool headless{false};
    std::string chd_path{"Original-Ants/ants.chd"};
    std::string default_map_path{"Original-Ants/Maps/TREASURE.LVL"};
    uint32_t random_seed{1337};
};

/**
 * @brief Master application lifecycle coordinator handling loop, events, sim, and audio.
 */
class Application {
public:
    Application();
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    bool init(int argc, char* argv[]);
    bool init(const ApplicationConfig& config);
    int run();
    void shutdown();

    bool is_running() const noexcept { return is_running_; }
    void quit() noexcept { is_running_ = false; }

    const ApplicationConfig& config() const noexcept { return config_; }
    ants::sim::SimulationEngine& sim() noexcept { return sim_; }
    Renderer& renderer() noexcept { return *renderer_; }

private:
    void handle_events();
    void handle_key_down(const SDL_KeyboardEvent& key);
    void handle_mouse_motion(const SDL_MouseMotionEvent& motion);
    void handle_mouse_button(const SDL_MouseButtonEvent& button);

    void update_simulation(float dt);
    void render_frame();

    ApplicationConfig config_{};
    bool is_running_{false};
    bool is_paused_{false};

    SDL_Window* window_{nullptr};
    std::unique_ptr<Renderer> renderer_;

    ants::assets::AssetArchive assets_;
    ants::assets::LevelData current_level_;
    ants::sim::SimulationEngine sim_;

    int32_t selected_unit_id_{-1};
    int32_t mouse_screen_x_{0};
    int32_t mouse_screen_y_{0};
    bool is_dragging_minimap_{false};

    // 20 Hz Discrete Simulation Timing
    uint64_t last_tick_time_{0};
    float tick_accumulator_{0.0f};
};

} // namespace ants::app
```

---

### 7.3 Blueprint 3: `src/ants_app/renderer.cpp`
```cpp
#include "ants_app/renderer.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace ants::app {

// ============================================================================
// ViewportCamera Implementation
// ============================================================================

void ViewportCamera::pan(float dx, float dy, float dt, uint32_t map_w, uint32_t map_h) {
    x += dx * scroll_speed * dt;
    y += dy * scroll_speed * dt;
    clamp_to_bounds(map_w, map_h);
}

void ViewportCamera::center_on(int32_t world_px, int32_t world_py, uint32_t map_w, uint32_t map_h) {
    x = static_cast<float>(world_px - PLAYFIELD_W / 2);
    y = static_cast<float>(world_py - PLAYFIELD_H / 2);
    clamp_to_bounds(map_w, map_h);
}

void ViewportCamera::clamp_to_bounds(uint32_t map_w, uint32_t map_h) {
    float max_x = std::max(0.0f, static_cast<float>(map_w * TILE_SIZE - PLAYFIELD_W));
    float max_y = std::max(0.0f, static_cast<float>(map_h * TILE_SIZE - PLAYFIELD_H));
    x = std::max(0.0f, std::min(x, max_x));
    y = std::max(0.0f, std::min(y, max_y));
}

bool ViewportCamera::world_to_screen(int32_t wx, int32_t wy, int32_t& sx, int32_t& sy) const noexcept {
    sx = PLAYFIELD_X + (wx - static_cast<int32_t>(x));
    sy = PLAYFIELD_Y + (wy - static_cast<int32_t>(y));
    return (sx >= PLAYFIELD_X - TILE_SIZE && sx <= PLAYFIELD_X + PLAYFIELD_W &&
            sy >= PLAYFIELD_Y - TILE_SIZE && sy <= PLAYFIELD_Y + PLAYFIELD_H);
}

bool ViewportCamera::screen_to_world(int32_t sx, int32_t sy, int32_t& wx, int32_t& wy) const noexcept {
    if (sx < PLAYFIELD_X || sx >= PLAYFIELD_X + PLAYFIELD_W ||
        sy < PLAYFIELD_Y || sy >= PLAYFIELD_Y + PLAYFIELD_H) {
        return false;
    }
    wx = static_cast<int32_t>(x) + (sx - PLAYFIELD_X);
    wy = static_cast<int32_t>(y) + (sy - PLAYFIELD_Y);
    return true;
}

bool ViewportCamera::is_tile_visible(int32_t tx, int32_t ty) const noexcept {
    int32_t px = tx * TILE_SIZE;
    int32_t py = ty * TILE_SIZE;
    return (px + TILE_SIZE >= x && px <= x + PLAYFIELD_W &&
            py + TILE_SIZE >= y && py <= y + PLAYFIELD_H);
}

// ============================================================================
// TextureCache Implementation
// ============================================================================

TextureCache::TextureCache(SDL_Renderer* renderer, const ants::assets::AssetArchive& archive)
    : renderer_(renderer), archive_(archive) {}

TextureCache::~TextureCache() {
    clear();
}

void TextureCache::clear() {
    for (auto& pair : textures_) {
        if (pair.second) {
            SDL_DestroyTexture(pair.second);
        }
    }
    textures_.clear();
}

SDL_Texture* TextureCache::get_sprite_texture(uint32_t sprite_id, bool mirrored) {
    uint64_t key = (static_cast<uint64_t>(sprite_id) << 1) | (mirrored ? 1 : 0);
    auto it = textures_.find(key);
    if (it != textures_.end()) {
        return it->second;
    }

    if (!renderer_) return nullptr;

    const auto& sp = mirrored ? archive_.get_mirrored_sprite(sprite_id)
                              : archive_.get_sprite(sprite_id);
    if (sp.width == 0 || sp.height == 0) return nullptr;

    // Convert 8-bit paletted sprite to 32-bit RGBA
    std::vector<uint8_t> rgba = sp.to_rgba32(archive_.get_palette());

    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormatFrom(
        rgba.data(),
        static_cast<int>(sp.width),
        static_cast<int>(sp.height),
        32,
        static_cast<int>(sp.width * 4),
        SDL_PIXELFORMAT_RGBA32
    );
    if (!surf) return nullptr;

    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
    SDL_FreeSurface(surf);

    if (tex) {
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        textures_[key] = tex;
    }
    return tex;
}

SDL_Texture* TextureCache::get_named_sprite_texture(const std::string& name, bool mirrored) {
    int32_t sid = archive_.find_sprite_id(name);
    if (sid < 0) {
        sid = archive_.find_sprite_id(name + ".bmp");
    }
    if (sid >= 0) {
        return get_sprite_texture(static_cast<uint32_t>(sid), mirrored);
    }
    return nullptr;
}

// ============================================================================
// Renderer Implementation
// ============================================================================

Renderer::Renderer() = default;

Renderer::~Renderer() {
    shutdown();
}

bool Renderer::init(SDL_Window* window,
                    const ants::assets::AssetArchive& archive,
                    bool integer_scale) {
    archive_ = &archive;

    renderer_ = SDL_CreateRenderer(
        window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );
    if (!renderer_) {
        // Fallback for headless/software mode
        renderer_ = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
        if (!renderer_) {
            std::cerr << "Failed to create SDL Renderer: " << SDL_GetError() << std::endl;
            return false;
        }
    }

    SDL_RenderSetLogicalSize(renderer_, CANVAS_WIDTH, CANVAS_HEIGHT);
    if (integer_scale) {
        SDL_RenderSetIntegerScale(renderer_, SDL_TRUE);
    }

    texture_cache_ = std::make_unique<TextureCache>(renderer_, archive);
    return true;
}

void Renderer::shutdown() {
    if (texture_cache_) {
        texture_cache_->clear();
        texture_cache_.reset();
    }
    if (renderer_) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
    archive_ = nullptr;
}

void Renderer::set_level(const ants::assets::LevelData& level) {
    map_width_ = level.width;
    map_height_ = level.height;
    camera_.clamp_to_bounds(map_width_, map_height_);

    // Pre-resolve tile dictionary strings to sprite IDs
    tile_sprite_ids_.assign(level.tile_dictionary.size(), -1);
    for (size_t i = 0; i < level.tile_dictionary.size(); ++i) {
        const std::string& name = level.tile_dictionary[i];
        if (name.empty() || name == ".") continue;

        int32_t sid = archive_->find_sprite_id(name);
        if (sid < 0) sid = archive_->find_sprite_id(name + ".bmp");
        tile_sprite_ids_[i] = sid;
    }
}

void Renderer::begin_frame() {
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255); // Black letterbox / background
    SDL_RenderClear(renderer_);
}

void Renderer::render_world(const ants::sim::WorldState& world,
                            const ants::sim::Grid& grid,
                            int32_t selected_unit_id) {
    render_queue_.clear();

    // 1. Clip exclusively to playfield
    SDL_Rect clip_rect = { PLAYFIELD_X, PLAYFIELD_Y, PLAYFIELD_W, PLAYFIELD_H };
    SDL_RenderSetClipRect(renderer_, &clip_rect);

    // 2. Layer 1 Terrain
    render_terrain_layer1(grid);

    // 3. Layer 2 Structures / Interactive Objects
    render_terrain_layer2_structures(grid);

    // 4. Ant Units (Depth-Sorted)
    render_ant_units(world, selected_unit_id);

    // 5. Unset clipping for HUD overlays
    SDL_RenderSetClipRect(renderer_, nullptr);
}

void Renderer::render_terrain_layer1(const ants::sim::Grid& grid) {
    int32_t start_col = std::max(0, static_cast<int32_t>(camera_.x) / TILE_SIZE);
    int32_t end_col   = std::min(static_cast<int32_t>(grid.width()) - 1,
                                 (static_cast<int32_t>(camera_.x) + PLAYFIELD_W + 31) / TILE_SIZE);
    int32_t start_row = std::max(0, static_cast<int32_t>(camera_.y) / TILE_SIZE);
    int32_t end_row   = std::min(static_cast<int32_t>(grid.height()) - 1,
                                 (static_cast<int32_t>(camera_.y) + PLAYFIELD_H + 31) / TILE_SIZE);

    for (int32_t r = start_row; r <= end_row; ++r) {
        for (int32_t c = start_col; c <= end_col; ++c) {
            const auto& cell = grid.get_cell(static_cast<uint32_t>(c), static_cast<uint32_t>(r));
            int32_t sx = 0, sy = 0;
            camera_.world_to_screen(c * TILE_SIZE, r * TILE_SIZE, sx, sy);

            SDL_Rect dst = { sx, sy, TILE_SIZE, TILE_SIZE };
            int32_t sid = (cell.terrain_id < tile_sprite_ids_.size()) ? tile_sprite_ids_[cell.terrain_id] : -1;

            if (sid >= 0) {
                SDL_Texture* tex = texture_cache_->get_sprite_texture(static_cast<uint32_t>(sid));
                if (tex) {
                    SDL_RenderCopy(renderer_, tex, nullptr, &dst);
                    continue;
                }
            }

            // Fallback colors for terrain category
            if (cell.terrain_type == ants::sim::TERRAIN_WATER) {
                SDL_SetRenderDrawColor(renderer_, 23, 71, 151, 255); // Navy Water
            } else if (cell.terrain_type == ants::sim::TERRAIN_OBSTACLE) {
                SDL_SetRenderDrawColor(renderer_, 47, 51, 63, 255);  // Dark Rock
            } else {
                SDL_SetRenderDrawColor(renderer_, 83, 147, 43, 255); // Olive Grass
            }
            SDL_RenderFillRect(renderer_, &dst);
        }
    }
}

void Renderer::render_terrain_layer2_structures(const ants::sim::Grid& grid) {
    int32_t start_col = std::max(0, static_cast<int32_t>(camera_.x) / TILE_SIZE);
    int32_t end_col   = std::min(static_cast<int32_t>(grid.width()) - 1,
                                 (static_cast<int32_t>(camera_.x) + PLAYFIELD_W + 31) / TILE_SIZE);
    int32_t start_row = std::max(0, static_cast<int32_t>(camera_.y) / TILE_SIZE);
    int32_t end_row   = std::min(static_cast<int32_t>(grid.height()) - 1,
                                 (static_cast<int32_t>(camera_.y) + PLAYFIELD_H + 31) / TILE_SIZE);

    for (int32_t r = start_row; r <= end_row; ++r) {
        for (int32_t c = start_col; c <= end_col; ++c) {
            const auto& cell = grid.get_cell(static_cast<uint32_t>(c), static_cast<uint32_t>(r));
            if (cell.is_empty_overlay()) continue;

            int32_t sx = 0, sy = 0;
            camera_.world_to_screen(c * TILE_SIZE, r * TILE_SIZE, sx, sy);
            SDL_Rect dst = { sx, sy, TILE_SIZE, TILE_SIZE };

            // 1. Bridges (Stages 1..4)
            if (cell.interactive_id >= ants::sim::TILE_BRIDGE1 &&
                cell.interactive_id <= ants::sim::TILE_BRIDGE4) {
                int stage = cell.interactive_id - ants::sim::TILE_BRIDGE1 + 1;
                std::string bname = (stage == 4) ? "bridge4a.bmp" : ("bridge" + std::to_string(stage) + ".bmp");
                SDL_Texture* tex = texture_cache_->get_named_sprite_texture(bname);
                if (tex) SDL_RenderCopy(renderer_, tex, nullptr, &dst);
                continue;
            }

            // 2. Fire Walls
            if (cell.has_fire()) {
                SDL_Texture* tex = texture_cache_->get_named_sprite_texture("9fire01.bmp");
                if (tex) SDL_RenderCopy(renderer_, tex, nullptr, &dst);
                continue;
            }

            // 3. Bombs
            if (cell.has_bomb()) {
                static const char* bomb_names[4] = { "1bombblk.bmp", "1bombblu.bmp", "1bombred.bmp", "1bombgrn.bmp" };
                uint8_t owner = cell.interactive_owner % 4;
                SDL_Texture* tex = texture_cache_->get_named_sprite_texture(bomb_names[owner]);
                if (tex) SDL_RenderCopy(renderer_, tex, nullptr, &dst);
                continue;
            }

            // 4. Dropped Lunchbox
            if (cell.has_lunchbox()) {
                SDL_Texture* tex = texture_cache_->get_named_sprite_texture("lunchicon.bmp");
                if (tex) SDL_RenderCopy(renderer_, tex, nullptr, &dst);
                continue;
            }

            // 5. Generic Food Items
            if (cell.has_food()) {
                SDL_Texture* tex = texture_cache_->get_named_sprite_texture("food.bmp");
                if (tex) SDL_RenderCopy(renderer_, tex, nullptr, &dst);
                continue;
            }
        }
    }

    // Anthill Bases
    for (const auto& a : grid.anthills()) {
        int32_t sx = 0, sy = 0;
        if (camera_.world_to_screen(a.x * TILE_SIZE, a.y * TILE_SIZE, sx, sy)) {
            static const char* hill_names[4] = { "bstart.bmp", "ustart.bmp", "rstart.bmp", "gstart.bmp" };
            SDL_Rect dst = { sx, sy, TILE_SIZE, TILE_SIZE };
            SDL_Texture* tex = texture_cache_->get_named_sprite_texture(hill_names[a.team_id % 4]);
            if (tex) SDL_RenderCopy(renderer_, tex, nullptr, &dst);
        }
    }
}

void Renderer::draw_ant_shadow(int32_t anchor_sx, int32_t anchor_sy, int32_t altitude_z) {
    SDL_Texture* shadow_tex = texture_cache_->get_named_sprite_texture("shadow.bmp");
    if (!shadow_tex) return;

    SDL_Rect dst = { anchor_sx - 16, anchor_sy - 15, 32, 31 };
    uint8_t alpha = static_cast<uint8_t>(std::max(40, 220 - altitude_z * 4));
    SDL_SetTextureAlphaMod(shadow_tex, alpha);
    SDL_RenderCopy(renderer_, shadow_tex, nullptr, &dst);
    SDL_SetTextureAlphaMod(shadow_tex, 255);
}

void Renderer::draw_single_ant(const ants::sim::AntSnapshot& ant, bool is_selected) {
    int32_t sx = 0, sy = 0;
    if (!camera_.world_to_screen(ant.px, ant.py, sx, sy)) return;

    // 1. Calculate Parabolic Elevation (Knockback Altitude)
    int32_t altitude_z = 0;
    if (ant.is_airborne) {
        // 10-tick flight arc, 36px apex
        int32_t t = static_cast<int32_t>(ant.anim_frame % 11);
        altitude_z = (4 * 36 * t * (10 - t)) / 100;
        draw_ant_shadow(sx, sy, altitude_z);
    }

    // 2. Resolve Action Animation Prefix
    static const char* class_prefixes[6] = { "ag", "ab", "af", "at", "ac", "as" };
    std::string prefix = class_prefixes[static_cast<size_t>(ant.type) % 6];
    std::string action = "st"; // Default Idle

    if (ant.anim_state == static_cast<uint16_t>(ants::sim::UnitState::Walking)) {
        action = ant.is_holding ? "ws" : (ant.is_swimming ? "sw" : "wg");
    } else if (ant.anim_state == static_cast<uint16_t>(ants::sim::UnitState::Attacking)) {
        action = "at";
    } else if (ant.anim_state == static_cast<uint16_t>(ants::sim::UnitState::Knockback)) {
        action = "gf";
    } else if (ant.anim_state == static_cast<uint16_t>(ants::sim::UnitState::Bounce)) {
        action = "gb";
    } else if (ant.anim_state == static_cast<uint16_t>(ants::sim::UnitState::Drowning)) {
        action = "dr";
    }

    ants::assets::Direction dir = static_cast<ants::assets::Direction>(ant.facing & 7);
    const auto* seq = archive_->get_directional_animation(prefix + action, dir);

    int32_t render_y = sy - altitude_z;

    if (seq && !seq->subitems.empty()) {
        size_t sub_idx = ant.anim_frame % seq->subitems.size();
        const auto& sub = seq->subitems[sub_idx];

        for (const auto& f : sub.frames) {
            bool mirrored = ants::assets::get_direction_mapping(dir).mirrored;
            SDL_Texture* tex = texture_cache_->get_sprite_texture(f.sprite_index, mirrored);
            if (!tex) continue;

            const auto& sp = mirrored ? archive_->get_mirrored_sprite(f.sprite_index)
                                      : archive_->get_sprite(f.sprite_index);
            SDL_Rect dst = { sx + f.dx, render_y + f.dy, static_cast<int>(sp.width), static_cast<int>(sp.height) };
            SDL_RenderCopy(renderer_, tex, nullptr, &dst);
        }
    } else {
        // Fallback: draw directional stand sprite
        std::string fallback_name = prefix + "st301.bmp";
        SDL_Texture* tex = texture_cache_->get_named_sprite_texture(fallback_name);
        if (tex) {
            SDL_Rect dst = { sx - 16, render_y - 16, 32, 32 };
            SDL_RenderCopy(renderer_, tex, nullptr, &dst);
        }
    }

    // 3. Selection Indicator & Health Bar
    if (is_selected) {
        SDL_SetRenderDrawColor(renderer_, 255, 255, 0, 255); // Yellow Selection Ring
        SDL_Rect sel_box = { sx - 14, render_y - 14, 28, 28 };
        SDL_RenderDrawRect(renderer_, &sel_box);

        // Health Bar (10 HP standard)
        SDL_Rect bar_bg = { sx - 12, render_y - 20, 24, 4 };
        SDL_SetRenderDrawColor(renderer_, 40, 40, 40, 200);
        SDL_RenderFillRect(renderer_, &bar_bg);

        int hp_w = (ant.max_hp > 0) ? (ant.hp * 24) / ant.max_hp : 0;
        SDL_Rect bar_fg = { sx - 12, render_y - 20, hp_w, 4 };
        if (ant.hp > 6) SDL_SetRenderDrawColor(renderer_, 50, 220, 50, 255);
        else if (ant.hp > 3) SDL_SetRenderDrawColor(renderer_, 230, 200, 30, 255);
        else SDL_SetRenderDrawColor(renderer_, 230, 40, 40, 255);
        SDL_RenderFillRect(renderer_, &bar_fg);
    }
}

void Renderer::render_ant_units(const ants::sim::WorldState& world, int32_t selected_unit_id) {
    for (const auto& a : world.ants) {
        if (a.is_underground) continue;

        RenderItem item{};
        item.sort_y = a.py;
        item.draw_func = [this, a, selected_unit_id](SDL_Renderer*, TextureCache&) {
            this->draw_single_ant(a, a.id == static_cast<uint32_t>(selected_unit_id));
        };
        render_queue_.push_back(item);
    }

    // Y-Sorting (Background to Foreground)
    std::sort(render_queue_.begin(), render_queue_.end(), [](const RenderItem& a, const RenderItem& b) {
        return a.sort_y < b.sort_y;
    });

    for (auto& item : render_queue_) {
        item.draw_func(renderer_, *texture_cache_);
    }
}

void Renderer::render_minimap(const ants::sim::WorldState& world, const ants::sim::Grid& grid) {
    SDL_Rect radar_rect = { MINIMAP_X, MINIMAP_Y, MINIMAP_W, MINIMAP_H };
    SDL_SetRenderDrawColor(renderer_, 20, 24, 28, 255);
    SDL_RenderFillRect(renderer_, &radar_rect);

    if (map_width_ == 0 || map_height_ == 0) return;

    float sx_scale = static_cast<float>(MINIMAP_W) / static_cast<float>(map_width_);
    float sy_scale = static_cast<float>(MINIMAP_H) / static_cast<float>(map_height_);

    // 1. Terrain Pass (Sampled)
    for (uint32_t y = 0; y < map_height_; y += 2) {
        for (uint32_t x = 0; x < map_width_; x += 2) {
            const auto& cell = grid.get_cell(x, y);
            if (cell.terrain_type == ants::sim::TERRAIN_WATER) {
                SDL_SetRenderDrawColor(renderer_, 23, 71, 151, 255);
            } else if (cell.terrain_type == ants::sim::TERRAIN_OBSTACLE) {
                SDL_SetRenderDrawColor(renderer_, 47, 51, 63, 255);
            } else {
                SDL_SetRenderDrawColor(renderer_, 83, 147, 43, 255);
            }
            SDL_Rect dot = {
                MINIMAP_X + static_cast<int>(x * sx_scale),
                MINIMAP_Y + static_cast<int>(y * sy_scale),
                std::max(1, static_cast<int>(sx_scale * 2.0f)),
                std::max(1, static_cast<int>(sy_scale * 2.0f))
            };
            SDL_RenderFillRect(renderer_, &dot);
        }
    }

    // 2. Anthills (Bases)
    for (const auto& hill : grid.anthills()) {
        static const SDL_Color base_colors[4] = {
            { 79, 87, 111, 255 },  // Team 0: Black
            { 119, 175, 239, 255 }, // Team 1: Blue
            { 251, 51, 91, 255 },   // Team 2: Red
            { 83, 147, 43, 255 }    // Team 3: Green
        };
        const auto& c = base_colors[hill.team_id % 4];
        SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, c.a);
        SDL_Rect bdot = {
            MINIMAP_X + static_cast<int>(hill.x * sx_scale) - 2,
            MINIMAP_Y + static_cast<int>(hill.y * sy_scale) - 2,
            5, 5
        };
        SDL_RenderFillRect(renderer_, &bdot);
    }

    // 3. Units Pass
    for (const auto& a : world.ants) {
        if (a.is_underground) continue;
        static const SDL_Color unit_colors[4] = {
            { 200, 200, 220, 255 }, // Black team light dot
            { 120, 180, 255, 255 }, // Blue
            { 255, 70, 70, 255 },   // Red
            { 90, 240, 90, 255 }    // Green
        };
        const auto& c = unit_colors[a.player_id % 4];
        SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, c.a);
        SDL_Rect udot = {
            MINIMAP_X + static_cast<int>((a.px / 32) * sx_scale),
            MINIMAP_Y + static_cast<int>((a.py / 32) * sy_scale),
            2, 2
        };
        SDL_RenderFillRect(renderer_, &udot);
    }

    // 4. Viewport Wireframe Box
    float map_w_px = static_cast<float>(map_width_ * TILE_SIZE);
    float map_h_px = static_cast<float>(map_height_ * TILE_SIZE);
    SDL_Rect cam_box = {
        MINIMAP_X + static_cast<int>((camera_.x / map_w_px) * MINIMAP_W),
        MINIMAP_Y + static_cast<int>((camera_.y / map_h_px) * MINIMAP_H),
        std::max(4, static_cast<int>((PLAYFIELD_W / map_w_px) * MINIMAP_W)),
        std::max(4, static_cast<int>((PLAYFIELD_H / map_h_px) * MINIMAP_H))
    };
    SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 255);
    SDL_RenderDrawRect(renderer_, &cam_box);
}

void Renderer::render_hud_chrome(const ants::sim::WorldState&, int32_t) {
    // Top Bar (640x22)
    SDL_Texture* top_tex = texture_cache_->get_named_sprite_texture("x0y0.bmp");
    if (top_tex) {
        SDL_Rect top_rect = { 0, 0, 640, 22 };
        SDL_RenderCopy(renderer_, top_tex, nullptr, &top_rect);
    }

    // Left Border (17x458)
    SDL_Texture* left_tex = texture_cache_->get_named_sprite_texture("x0y22.bmp");
    if (left_tex) {
        SDL_Rect left_rect = { 0, 22, 17, 458 };
        SDL_RenderCopy(renderer_, left_tex, nullptr, &left_rect);
    }

    // Right Divider (22x426)
    SDL_Texture* div_tex = texture_cache_->get_named_sprite_texture("x458y35.bmp");
    if (div_tex) {
        SDL_Rect div_rect = { 458, 35, 22, 426 };
        SDL_RenderCopy(renderer_, div_tex, nullptr, &div_rect);
    }

    // Bottom News Banner (623x19)
    SDL_Texture* bot_tex = texture_cache_->get_named_sprite_texture("x17y461.bmp");
    if (bot_tex) {
        SDL_Rect bot_rect = { 17, 461, 623, 19 };
        SDL_RenderCopy(renderer_, bot_tex, nullptr, &bot_rect);
    }

    // Selection Card Backing (160x128)
    SDL_Texture* card_tex = texture_cache_->get_named_sprite_texture("x480y126.bmp");
    if (card_tex) {
        SDL_Rect card_rect = { CARD_X, CARD_Y, CARD_W, CARD_H };
        SDL_RenderCopy(renderer_, card_tex, nullptr, &card_rect);
    }
}

void Renderer::end_frame() {
    SDL_RenderPresent(renderer_);
}

} // namespace ants::app
```

---

### 7.4 Blueprint 4: `src/ants_app/application.cpp`
```cpp
#include "ants_app/application.hpp"
#include <iostream>
#include <cstring>

namespace ants::app {

Application::Application() = default;

Application::~Application() {
    shutdown();
}

bool Application::init(int argc, char* argv[]) {
    ApplicationConfig cfg{};

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--headless") == 0) {
            cfg.headless = true;
        } else if (std::strcmp(argv[i], "--map") == 0 && i + 1 < argc) {
            cfg.default_map_path = argv[++i];
        } else if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            cfg.random_seed = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (std::strcmp(argv[i], "--fullscreen") == 0) {
            cfg.fullscreen = true;
        }
    }
    return init(cfg);
}

bool Application::init(const ApplicationConfig& config) {
    config_ = config;

    // 1. Initialize SDL2
    uint32_t sdl_flags = SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER;
    if (config_.headless) {
        SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
    }

    if (SDL_Init(sdl_flags) != 0) {
        std::cerr << "SDL_Init Error: " << SDL_GetError() << std::endl;
        return false;
    }

    // 2. Load Master Asset Archive (ants.chd)
    if (!assets_.load_from_file(config_.chd_path)) {
        std::cerr << "Failed to load CHD archive: " << config_.chd_path << std::endl;
        return false;
    }

    // 3. Load Map Level
    if (!current_level_.load_from_file(config_.default_map_path)) {
        std::cerr << "Failed to load Level: " << config_.default_map_path << std::endl;
        return false;
    }

    // 4. Initialize Simulation Engine
    sim_.init(current_level_, config_.random_seed);

    // 5. Create Desktop Window
    uint32_t win_flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    if (config_.fullscreen) win_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    if (config_.headless)   win_flags = SDL_WINDOW_HIDDEN;

    window_ = SDL_CreateWindow(
        config_.title.c_str(),
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        config_.window_width, config_.window_height,
        win_flags
    );
    if (!window_) {
        std::cerr << "Failed to create SDL Window: " << SDL_GetError() << std::endl;
        return false;
    }

    // 6. Initialize Renderer
    renderer_ = std::make_unique<Renderer>();
    if (!renderer_->init(window_, assets_, config_.integer_scaling)) {
        std::cerr << "Failed to initialize Renderer" << std::endl;
        return false;
    }

    renderer_->set_level(current_level_);

    // Center camera on Player 0's base
    const auto* spawn = current_level_.anthill_spawns.empty() ? nullptr : &current_level_.anthill_spawns[0];
    if (spawn) {
        renderer_->camera().center_on(spawn->x * TILE_SIZE, spawn->y * TILE_SIZE,
                                      current_level_.width, current_level_.height);
    }

    is_running_ = true;
    last_tick_time_ = SDL_GetPerformanceCounter();
    return true;
}

void Application::shutdown() {
    is_running_ = false;
    if (renderer_) {
        renderer_->shutdown();
        renderer_.reset();
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    SDL_Quit();
}

int Application::run() {
    uint64_t perf_freq = SDL_GetPerformanceFrequency();
    uint64_t last_frame_time = SDL_GetPerformanceCounter();

    while (is_running_) {
        uint64_t current_time = SDL_GetPerformanceCounter();
        float delta_time = static_cast<float>(current_time - last_frame_time) / static_cast<float>(perf_freq);
        last_frame_time = current_time;

        handle_events();

        if (!is_paused_) {
            update_simulation(delta_time);
        }

        render_frame();

        if (config_.headless) {
            // Headless verification pass runs 10 frames and completes
            static int headless_frames = 0;
            if (++headless_frames >= 10) {
                is_running_ = false;
            }
        }
    }
    return 0;
}

void Application::handle_events() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                quit();
                break;
            case SDL_KEYDOWN:
                handle_key_down(event.key);
                break;
            case SDL_MOUSEMOTION:
                handle_mouse_motion(event.motion);
                break;
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
                handle_mouse_button(event.button);
                break;
            default:
                break;
        }
    }

    // Smooth Keyboard Panning
    const uint8_t* keystate = SDL_GetKeyboardState(nullptr);
    float pan_x = 0.0f, pan_y = 0.0f;
    if (keystate[SDL_SCANCODE_UP] || keystate[SDL_SCANCODE_W]) pan_y -= 1.0f;
    if (keystate[SDL_SCANCODE_DOWN] || keystate[SDL_SCANCODE_S]) pan_y += 1.0f;
    if (keystate[SDL_SCANCODE_LEFT] || keystate[SDL_SCANCODE_A]) pan_x -= 1.0f;
    if (keystate[SDL_SCANCODE_RIGHT] || keystate[SDL_SCANCODE_D]) pan_x += 1.0f;

    if (pan_x != 0.0f || pan_y != 0.0f) {
        renderer_->camera().pan(pan_x, pan_y, 0.016f, current_level_.width, current_level_.height);
    }
}

void Application::handle_key_down(const SDL_KeyboardEvent& key) {
    if (key.keysym.sym == SDLK_ESCAPE) {
        quit();
    } else if (key.keysym.sym == SDLK_SPACE) {
        // Center on selected unit or base
        if (selected_unit_id_ >= 0) {
            const auto& world = sim_.get_world_state();
            for (const auto& a : world.ants) {
                if (a.id == static_cast<uint32_t>(selected_unit_id_)) {
                    renderer_->camera().center_on(a.px, a.py, current_level_.width, current_level_.height);
                    break;
                }
            }
        }
    } else if (key.keysym.sym == SDLK_h) {
        // Hatch Worker Ant
        sim_.hatch_ant(0, ants::sim::AntType::Worker);
    }
}

void Application::handle_mouse_motion(const SDL_MouseMotionEvent& motion) {
    mouse_screen_x_ = motion.x;
    mouse_screen_y_ = motion.y;

    if (is_dragging_minimap_) {
        if (mouse_screen_x_ >= MINIMAP_X && mouse_screen_x_ < MINIMAP_X + MINIMAP_W &&
            mouse_screen_y_ >= MINIMAP_Y && mouse_screen_y_ < MINIMAP_Y + MINIMAP_H) {
            float frac_x = static_cast<float>(mouse_screen_x_ - MINIMAP_X) / static_cast<float>(MINIMAP_W);
            float frac_y = static_cast<float>(mouse_screen_y_ - MINIMAP_Y) / static_cast<float>(MINIMAP_H);
            int32_t target_wx = static_cast<int32_t>(frac_x * current_level_.width * TILE_SIZE);
            int32_t target_wy = static_cast<int32_t>(frac_y * current_level_.height * TILE_SIZE);
            renderer_->camera().center_on(target_wx, target_wy, current_level_.width, current_level_.height);
        }
    }
}

void Application::handle_mouse_button(const SDL_MouseButtonEvent& button) {
    if (button.button == SDL_BUTTON_LEFT) {
        if (button.type == SDL_MOUSEBUTTONDOWN) {
            // Check Minimap click
            if (button.x >= MINIMAP_X && button.x < MINIMAP_X + MINIMAP_W &&
                button.y >= MINIMAP_Y && button.y < MINIMAP_Y + MINIMAP_H) {
                is_dragging_minimap_ = true;
                float frac_x = static_cast<float>(button.x - MINIMAP_X) / static_cast<float>(MINIMAP_W);
                float frac_y = static_cast<float>(button.y - MINIMAP_Y) / static_cast<float>(MINIMAP_H);
                int32_t target_wx = static_cast<int32_t>(frac_x * current_level_.width * TILE_SIZE);
                int32_t target_wy = static_cast<int32_t>(frac_y * current_level_.height * TILE_SIZE);
                renderer_->camera().center_on(target_wx, target_wy, current_level_.width, current_level_.height);
                return;
            }

            // Playfield unit selection
            int32_t wx = 0, wy = 0;
            if (renderer_->camera().screen_to_world(button.x, button.y, wx, wy)) {
                const auto& world = sim_.get_world_state();
                selected_unit_id_ = -1;
                for (const auto& a : world.ants) {
                    if (std::abs(a.px - wx) <= 16 && std::abs(a.py - wy) <= 16) {
                        selected_unit_id_ = static_cast<int32_t>(a.id);
                        break;
                    }
                }
            }
        } else if (button.type == SDL_MOUSEBUTTONUP) {
            is_dragging_minimap_ = false;
        }
    } else if (button.button == SDL_BUTTON_RIGHT && button.type == SDL_MOUSEBUTTONDOWN) {
        // Issue Movement Order to selected ant
        if (selected_unit_id_ >= 0) {
            int32_t wx = 0, wy = 0;
            if (renderer_->camera().screen_to_world(button.x, button.y, wx, wy)) {
                ants::sim::AntOrder order{};
                order.ant_id = static_cast<uint32_t>(selected_unit_id_);
                order.type = ants::sim::OrderType::Move;
                order.target_x = wx / TILE_SIZE;
                order.target_y = wy / TILE_SIZE;
                sim_.issue_order(order);
            }
        }
    }
}

void Application::update_simulation(float dt) {
    // Fixed 20 Hz Discrete Simulation Timestep (50 ms)
    tick_accumulator_ += dt;
    while (tick_accumulator_ >= 0.050f) {
        sim_.tick();
        tick_accumulator_ -= 0.050f;

        // Drain audio events
        auto audio_events = sim_.poll_audio_events();
        // Route audio events to AudioMixer...

        // Drain news flash events
        auto news_events = sim_.poll_news_events();
        // Route news events to NewsBanner...
    }
}

void Application::render_frame() {
    renderer_->begin_frame();

    const auto& world = sim_.get_world_state();
    renderer_->render_world(world, sim_.grid(), selected_unit_id_);
    renderer_->render_minimap(world, sim_.grid());
    renderer_->render_hud_chrome(world, selected_unit_id_);

    renderer_->end_frame();
}

} // namespace ants::app
```

---

### 7.5 CMake Integration Blueprint

To cleanly build `ants-app` as an executable and library:

#### In `src/ants_app/CMakeLists.txt`:
```cmake
find_package(SDL2 REQUIRED)

add_executable(ants-app
    main.cpp
    application.cpp
    renderer.cpp
)

target_include_directories(ants-app PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(ants-app PRIVATE
    ants_assets
    ants_sim
    ${SDL2_LIBRARIES}
    "-framework AudioToolbox"
    "-framework CoreAudio"
    "-framework Cocoa"
)
```

#### In Root `CMakeLists.txt`:
```cmake
add_subdirectory(src/ants_app)
```

---

## 8. Verification & Test Plan

1. **Windowing & Scaling Verification:**
   - Verify `SDL_RenderSetLogicalSize(renderer, 640, 480)` and `SDL_RenderSetIntegerScale` enforce black borders (pillarbox/letterbox) preserving exact 4:3 across window resizing (640x480, 1280x960, 1920x1080).
2. **Headless Execution Verification:**
   - Launch with `./ants-app --headless` in CI; verify initialization, 10 frames of execution, and clean shutdown with exit code 0.
3. **Camera Bounds Clamping:**
   - Pan camera to negative coordinates $(-100, -100)$ and beyond map edges $(2500, 2500)$; assert camera clamps within $[0, W_{\text{world}} - 441]$ and $[0, H_{\text{world}} - 439]$.
4. **Terrain Layer Compositing:**
   - Inspect Layer 1 base tile dictionary lookup and verify 100% dictionary name matching against CHD textures.
   - Verify Layer 2 objects (Bridges 1..4, Bombs, Fire walls, Food, Anthills) composite seamlessly.
5. **Sprite Mirroring & Elevation Verification:**
   - Verify ants facing West, North-West, and South-West render using precomputed horizontally mirrored sprites with render offset $dx' = -(dx + W)$.
   - Verify ants in knockback render with parabolic altitude offset $z \in [0, 36]$ with ground shadow.
