# Ants Remake — Milestone 3 HUD, UI Controls, Input Dispatch & Scorecard Modal Plan

**Document Version:** 1.0  
**Author:** explorer_m3_2 (M3 Explorer 2: HUD, UI Controls, Input Dispatch & Scorecard Modal)  
**Target Milestone:** Milestone 3 (`ants-app`)  
**Host Architecture:** macOS 15.x / Darwin 25.6.0 (Apple Silicon arm64, Apple Clang C++17/C++20, SDL2)

---

## 1. Executive Summary & Architectural Overview

The user interface subsystem for *Ants* (`ants-app`) is responsible for delivering the authentic 1995/1998 MSN Gaming Zone user interface at 640×480 logical resolution, integer-scaled to modern displays. The UI is completely decoupled from the deterministic simulation core (`ants-sim`) and asset decoder (`ants-assets`), interacting solely through clean data contracts, event queues, and state snapshots.

```
+-----------------------------------------------------------------------------------------+
|                                    640x480 Virtual Canvas                               |
|                                                                                         |
|  [Top Bar (0,0)-(640,22)] x0y0.bmp | Live Multi-Faction Scores | Match Clock [mm:ss]   |
+-------------------------------------------------------------+---------------------------+
|                                                             | [Minimap Radar (480,22)]  |
|                                                             |  Overview, Frustum Box,   |
|                                                             |  Click-to-Navigate        |
|                                                             +---------------------------+
|                                                             | [Selection Card (480,126)]|
|                                                             |  Portrait, HP Bar, Type,  |
|                   PLAYFIELD VIEWPORT                        |  Status, Lunchbox Icon    |
|               (17, 22) - (458, 461)                         +---------------------------+
|              Dimensions: 441 x 439 px                       | [Hatch Controls (480,254)]|
|                                                             |  Hatch Btn, Egg Pile,     |
|                                                             |  Incubation Progress      |
|                                                             +---------------------------+
|                                                             | [Action Bar (480,360)]    |
|                                                             |  Move, Attack, Bomb,      |
|                                                             |  Fire, Bridge, Thief, Can |
+-------------------------------------------------------------+---------------------------+
|  [Bottom Bar (17,461)-(640,480)] x17y461.bmp | News Flash Alert Banner & Notifications  |
+-----------------------------------------------------------------------------------------+
|  [MODAL OVERLAY (When Match Time = 0:00)] Results Scorecard (re_screen / Animation 25)  |
|  Full-Screen Clay Backdrop | Winner Banner | 4 Stats Columns | OK & Replay Controls      |
+-----------------------------------------------------------------------------------------+
```

---

## 2. Comprehensive Subsystem Specifications

### 2.1 Viewport, Coordinate Systems & Integer Pixel Scaler Integration
- **Virtual Logical Resolution:** 640 × 480 pixels (exact authentic 4:3 presentation).
- **Physical Scaling:** `ViewportScaler` computes the maximum integer factor `scale_factor = min(win_w / 640, win_h / 480)`.
- **Coordinate Translation:**
  ```cpp
  Vec2i logical = scaler.screen_to_logical(mouse_screen_x, mouse_screen_y);
  ```
- **Hit-Test Regions:**
  - `Top Bar`: `(0, 0)` to `(640, 22)`
  - `Playfield Viewport`: `(17, 22)` to `(458, 461)` (441 × 439 px)
  - `Minimap / Radar`: `(480, 22)` to `(640, 126)`
  - `Selection Card`: `(480, 126)` to `(640, 254)`
  - `Hatch Controls`: `(480, 254)` to `(640, 360)`
  - `Action Command Bar`: `(480, 360)` to `(640, 461)`
  - `News Flash Banner`: `(17, 461)` to `(640, 480)`
  - `Scorecard Modal`: Full-screen `(0, 0)` to `(640, 480)` when active.

---

### 2.2 Minimap / Radar Subsystem
- **Screen Location:** X: `480` to `640`, Y: `22` to `126`.
- **Radar Display Box:** `(490, 26)` to `(630, 122)` (Size: 140 × 96 px).
- **Backing Sprite:** `Sprite 2713: x599y35.bmp` (41 × 91) at `(599, 35)`.
- **Terrain Overview Pass:**
  - Translates map grid dimensions (31×31, 40×40, or 60×60 tiles) into radar display pixels.
  - Authentic color mapping:
    - Grass / Clear ground: Olive green (`RGB(83, 147, 43)`)
    - Dirt / Mud / Clay: Terracotta brown (`RGB(175, 107, 75)`)
    - Water: Navy blue (`RGB(23, 71, 151)`)
    - Obstacles / Rock: Dark slate gray (`RGB(47, 51, 63)`)
- **Anthill Icons Pass:**
  - Base positions rendered as 5×5 diamond markers in team colors:
    - Team 0 (Black): `RGB(79, 87, 111)`
    - Team 1 (Blue): `RGB(119, 175, 239)`
    - Team 2 (Red): `RGB(251, 51, 91)`
    - Team 3 (Green): `RGB(83, 147, 43)`
- **Live Ant Units Pass:**
  - Active ants rendered as 2×2 bright pixels in team colors.
  - Selected ant highlighted with pulsing white/cyan dot.
- **Food & Item Indicators:**
  - Dropped food pieces and lunchboxes rendered as bright golden dots (`RGB(255, 215, 0)`).
- **Camera Viewport Frustum Box:**
  - 1-pixel bright white wireframe rectangle indicating current camera playfield view on world grid.
  - Rect boundaries dynamically derived from `camera.world_x`, `camera.world_y`, and playfield dimensions (441 × 439 px).
- **Click & Drag Navigation:**
  - Left-clicking or dragging within radar translates coordinate `(rx, ry)` to world grid tile `(tx, ty)`.
  - Tile coordinate is clamped: `clamped_x = std::max(0, std::min(map_w - 1, tx))`.
  - Centers camera on `(clamped_x, clamped_y)` immediately.

---

### 2.3 Ant Selection Card Subsystem
- **Screen Location:** X: `480`, Y: `126` (Dimensions: 160 × 128 px).
- **Backing Sprite:** `Sprite 2718: x480y126.bmp` (160 × 128) at `(480, 126)`.
- **Selection State:**
  - If no ant selected (`selected_ant_id == 0`): displays "No Selection" or colony overview.
  - If ant selected: displays full unit telemetry.
- **Ant Unit Portrait:**
  - Portrait frame centered at `(530, 144)`.
  - Class portrait mapping:
    - Worker Ant (`AntType::Worker`): `Sprite 1481: agst301.bmp` (23 × 40)
    - Bomber Ant (`AntType::Bomber`): `Sprite 1332: abst301.bmp` (21 × 40)
    - Fire Ant (`AntType::Fire`): `Sprite 968: afst301.bmp` (31 × 41)
    - Combat Ant (`AntType::Combat`): `Sprite 1807: acst301.bmp` (60 × 38)
    - Swimmer Ant (`AntType::Swimmer`): `Sprite 2014: asst301.bmp` (30 × 40)
    - Thief Ant (`AntType::Thief`): `Sprite 2470: atst301.bmp` (32 × 35)
- **Class Title Header:**
  - Header background: `Sprite 2711: wtype.bmp` (143 × 14) at `(488, 128)`.
  - Centered text: "Worker Ant", "Bomber Ant", "Combat Ant", "Fire Ant", "Swimmer Ant", "Thief Ant".
- **Health Bar:**
  - Position: `(496, 188)`, Width: 128 px, Height: 8 px.
  - 10 segments (1 segment per 1 HP, max 10 HP).
  - Segment color threshold:
    - 8–10 HP: Green (`RGB(0, 200, 0)`)
    - 4–7 HP: Yellow (`RGB(220, 200, 0)`)
    - 1–3 HP: Red (`RGB(220, 30, 30)`)
  - Text overlay: `"HP: X / 10"`
- **Action Status Text:**
  - Status background: `Sprite 2710: wstatus.bmp` (143 × 14) at `(488, 204)`.
  - Dynamic status label: `"Idle"`, `"Moving"`, `"Attacking"`, `"Planting Bomb"`, `"Defusing Bomb"`, `"Igniting Fire"`, `"Extinguishing"`, `"Building Bridge"`, `"Carrying Food"`, `"Infiltrating"`.
- **Held Item Indicator:**
  - When unit is holding food or stolen points (`is_holding == true`):
    - Displays `Sprite 2694: lunchicon.bmp` (34 × 40) at `(592, 144)`.
    - If carrying points: displays `"+50 pts"` badge.

---

### 2.4 Hatch Controls & Egg Pile Subsystem
- **Screen Location:** X: `480` to `640`, Y: `254` to `360`.
- **Backing Sprites:** `Sprite 2717: x480y266.bmp` (141 × 33) at `(480, 266)` and `Sprite 2714: x521y254.bmp` (19 × 182) at `(521, 254)`.
- **Hatch Button:**
  - Position: `(492, 262)` (Dimensions: 40 × 30 px).
  - Label: `Sprite 2682: labhatch.bmp` ("HATCH", 36 × 11) at `(494, 272)`.
  - Button Up: `Sprite 2683: buthatup.bmp` (23 × 26).
  - Button Down: `Sprite 2684: buthatd.bmp` (24 × 26).
  - Cost: `"200 pts"` displayed in gold font at `(538, 272)`.
  - Deduction check: Enabled ONLY when `player_score >= 200 && player_eggs > 0`.
  - If disabled: button rendered in depressed/dimmed state; click plays invalid buzz or no-op.
  - If enabled: click issues `sim.hatch_ant(player_id, AntType::Worker)`, plays Sound 0 (`buttonclick.wav`).
- **Egg Pile Visualization:**
  - Position: `(496, 302)`.
  - Dynamic visual threshold based on remaining eggs:
    - ≥ 8 eggs: `Sprite 554: eggs.bmp` (124 × 84)
    - 5–7 eggs: `Sprite 555: eggsa.bmp` (124 × 82)
    - 2–4 eggs: `Sprite 556: eggsb.bmp` (118 × 75)
    - 1 egg: `Sprite 557: eggsc.bmp` (83 × 35) or `Sprite 2693: egg.bmp` (12 × 16)
    - 0 eggs: empty ground.
  - Egg numerical counter: `"Eggs: N"` rendered at `(560, 340)`.
- **Incubation Progress:**
  - When hatching an egg, an incubation counter ticks down (e.g. 60 ticks = 3.0 seconds).
  - Plays egg cracking animation (`Sprite 338..340: eggh1..eggh3.bmp`).
  - Upon completion, ant emerges from base with Sound 87 (`scoreup.wav`).

---

### 2.5 Action Command Bar Subsystem
- **Screen Location:** X: `480` to `640`, Y: `360` to `460`.
- **Buttons Grid (7 Command Buttons):**
  1. **Move:** `Sprite 2573: butmovu.bmp` / `Sprite 2585: butmovd.bmp`, Label `Sprite 2572: labmov.bmp` (Hotkey: M).
  2. **Attack:** `Sprite 2580: butattu.bmp` / `Sprite 2589: butattd.bmp`, Label `Sprite 2579: labatt.bmp` (Hotkey: A).
  3. **Bomb:** `Sprite 2581: butbomu.bmp` / `Sprite 2590: butbomd.bmp`, Label `Sprite 2582: labbom.bmp` (Hotkey: B). Enabled only for Bomber Ant.
  4. **Fire:** `Sprite 2584: butfireu.bmp`, Label `Sprite 2583: labfire.bmp` (Hotkey: F). Enabled only for Fire Ant.
  5. **Bridge / Swim:** `Sprite 2576: butdipu.bmp` / `Sprite 2587: butdipd.bmp`, Label `Sprite 2575: labdib.bmp` (Hotkey: S). Enabled only for Swimmer Ant.
  6. **Thief Steal:** `Sprite 2577: butthfu.bmp` / `Sprite 2588: butthfd.bmp`, Label `Sprite 2578: labthf.bmp` (Hotkey: T). Enabled only for Thief Ant.
  7. **Cancel:** `Sprite 2706: butcanu.bmp` / `Sprite 2707: butcand.bmp`, Label `Sprite 2705: labcan.bmp` (Hotkey: Esc / C).
- **Active Order Highlighting:**
  - Clicking an action button puts the HUD into `OrderTargeting` mode.
  - Active button displays depressed sprite and golden border outline.
  - Mouse cursor over playfield switches to targeting reticle.
  - Left-clicking on playfield dispatches targeted order to `SimulationEngine::issue_order`.
  - Right-clicking or pressing Cancel resets mode back to normal.

---

### 2.6 News Flash Banner & Chat Subsystem
- **Screen Location:** Bottom bar X: `17` to `640`, Y: `461` to `480` (Height: 19 px).
- **Backing Sprite:** `Sprite 2708: x17y461.bmp` (623 × 19) at `(17, 461)`.
- **Message Queue & Transitions:**
  - FIFO queue holding up to 32 alerts.
  - Each message has a display lifetime of 100 ticks (5.0 seconds at 20 Hz).
  - Smooth dismiss transition (fade or swipe) when timer reaches 0.
  - Idle state: Displays `"Ants Remake"` when queue is empty.
- **Reverse-Engineered String Ingestion:**
  - Thief Base Alarm: `"[mm:ss] A ThiefAnt is at your anthill!"` (String ID 53, blinking red text).
  - Food Stolen: `"Food stolen..."` (String ID 62).
  - Alliance Formed: `"[mm:ss] %s and %s have formed an alliance!"` (String ID 39).
  - Alliance Broken: `"[mm:ss] %s broke their alliance with %s!"` (String ID 40).
  - Alliance Declined: `"%s declined the alliance invitation."` (String ID 80).

---

### 2.7 Match Clock & Scoreboard HUD
- **Screen Location:** Top bar X: `0` to `640`, Y: `0` to `22`.
- **Backing Sprite:** `Sprite 2709: x0y0.bmp` (640 × 22) at `(0, 0)`.
- **Match Clock:**
  - Centered at `(300, 5)`.
  - Formatted as `[MM:SS]` using sprite digits:
    - `Sprite 2722..2731: dig0.bmp..dig9.bmp` (7 × 10)
    - `Sprite 2732: digc.bmp` (7 × 12)
  - Color Warning: When remaining time < 60,000 ms (under 1 minute), digits turn red (or blink red/yellow).
  - At 0:00: Displays `"00:00"` and remains frozen.
- **Scoreboard HUD:**
  - Left of clock: Team 0 (Black) & Team 1 (Blue)
  - Right of clock: Team 2 (Red) & Team 3 (Green)
  - Each team entry: Color square, Name, and Display Score (`sim.get_display_score(p)`).
  - Dynamic Alliance Grouping: If Team 0 and Team 1 form an alliance, their scores combine and display an alliance bracket `[Black + Blue: 450]`.

---

### 2.8 Results Scorecard Modal (`re_screen` / Animation 25)
- **Trigger:** Immediate simulation freeze when `sim.is_match_over() == true` (time reaches 0:00).
- **Audio Split Routing:**
  - Winning player/team hears Sound 56 (`winner.wav`, 4.67s fanfare).
  - Defeated players hear Sound 41 (`playerout.wav`, 0.94s defeat sting).
- **Exact Reverse-Engineered Visual Layout (640×480 Canvas):**
  - Full-screen clay backdrop: tiled `Sprite 2: dclay96.bmp` (96 × 96) with beveled frames `dfram*.bmp`.
  - Top Banner: `Sprite 99: resbanr.bmp` (340 × 34) at `(140, 0)`
  - Title Graphic: `Sprite 98: yoscore.bmp` (302 × 127) at `(41, 55)`
  - Stats Headers Graphic: `Sprite 97: newstats.bmp` (259 × 133) at `(342, 84)`
    - Arrows pointing rightward directly to the 4 statistic columns:
      * Column 1 (X ≈ 496): **Score**
      * Column 2 (X ≈ 536): **Friendly Ants Lost**
      * Column 3 (X ≈ 557): **Enemy Ants Killed**
      * Column 4 (X ≈ 578): **New Ants Hatched**
  - Winner Section:
    - Header: `Sprite 96: winnr.bmp` (117 × 19) at `(40, 195)`
    - Box Frame: `Sprite 93: bg50x100.bmp` (stretched/tiled to 558 × 50) at `(40, 222)`
    - Winner Row: Displays winning player/alliance name, badge, and the 4 tracked stats.
  - Other Players Section:
    - Header: `Sprite 95: otherp.bmp` (203 × 25) at `(40, 280)`
    - Box Frame: `Sprite 94: efrbg100.bmp` (stretched/tiled to 558 × 150) at `(40, 310)`
    - Up to 3 rows of remaining players sorted by final score.
  - Interactive Action Buttons:
    - OK / Play Again Button: `Sprite 74: dbutoku.bmp` (46 × 20) at `(530, 445)`
    - Quit / Return Button: `Sprite 70: breturn1.bmp` (98 × 26) or `Sprite 81: dbutoffu.bmp` (46 × 20) at `(420, 445)`
  - Input Isolation: When modal is open, all playfield click events are completely blocked.

---

### 2.9 Mouse & Keyboard Input Dispatcher
- **Hit-Test Resolution:**
  1. Scorecard Modal (if active, consumes all clicks).
  2. Minimap Radar -> translates to camera repositioning.
  3. Hatch Button -> dispatches hatch order.
  4. Action Command Buttons -> sets active order targeting mode.
  5. Playfield Viewport:
     - **Left-Click:** Select unit under cursor; if targeting mode active, dispatch order.
     - **Left-Click Drag:** Marquee box selection. On release, selects all friendly units within box.
     - **Right-Click:** Contextual order dispatch:
       * Enemy ant -> Attack
       * Enemy anthill -> Infiltrate (Thief) or propose alliance
       * Friendly anthill -> Return to base (deposit/heal)
       * Bomb tile -> Defuse bomb (Bomber)
       * Fire tile -> Extinguish fire (Fire Ant)
       * Water tile -> Build bridge (Swimmer)
       * Empty ground -> Move order
- **Keyboard Shortcuts:**
  - `M`: Move mode
  - `A`: Attack mode
  - `B`: Bomb mode (Bomber only)
  - `F`: Fire mode (Fire only)
  - `S`: Bridge/Swim mode (Swimmer only)
  - `T`: Thief mode (Thief only)
  - `H`: Hatch ant
  - `Esc` / `C`: Cancel mode / Deselect
  - `Space`: Center camera on selected ant
  - `Tab`: Cycle through friendly units
  - `Arrow Keys` / `WASD`: Smooth camera panning

---

## 3. C++ Header Blueprint: `include/ants_app/hud.hpp`

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <deque>
#include <memory>
#include <array>
#include <functional>

#include "ants_assets/asset_archive.hpp"
#include "ants_sim/sim_engine.hpp"

namespace ants::app {

// Forward declarations
class IRenderer;

/**
 * @brief Active input modes for the playfield and HUD.
 */
enum class InputMode : uint8_t {
    Normal = 0,
    OrderTargeting,
    MarqueeSelecting
};

/**
 * @brief HUD Action command button descriptors.
 */
enum class ActionButtonId : uint8_t {
    Move = 0,
    Attack,
    Bomb,
    Fire,
    Bridge,
    Thief,
    Cancel,
    Count
};

/**
 * @brief Camera viewport geometry representation.
 */
struct Camera {
    int32_t world_x{0};
    int32_t world_y{0};
    int32_t viewport_w{441};
    int32_t viewport_h{439};

    void center_on(int32_t target_x, int32_t target_y, int32_t map_w_px, int32_t map_h_px) noexcept {
        world_x = target_x - (viewport_w / 2);
        world_y = target_y - (viewport_h / 2);
        clamp(map_w_px, map_h_px);
    }

    void clamp(int32_t map_w_px, int32_t map_h_px) noexcept {
        if (world_x < 0) world_x = 0;
        if (world_y < 0) world_y = 0;
        int32_t max_x = (map_w_px > viewport_w) ? (map_w_px - viewport_w) : 0;
        int32_t max_y = (map_h_px > viewport_h) ? (map_h_px - viewport_h) : 0;
        if (world_x > max_x) world_x = max_x;
        if (world_y > max_y) world_y = max_y;
    }
};

/**
 * @brief Item queued in the news flash alert banner.
 */
struct NewsBannerItem {
    std::string text;
    uint32_t remaining_ticks{100}; // 5.0 seconds default
    bool is_alarm{false};          // Blinks red for Thief infiltration
    uint8_t alpha{255};
};

/**
 * @brief Interactive button state descriptor.
 */
struct UIButton {
    int32_t x{0};
    int32_t y{0};
    int32_t w{0};
    int32_t h{0};
    uint32_t sprite_up{0};
    uint32_t sprite_down{0};
    uint32_t sprite_label{0};
    bool is_pressed{false};
    bool is_enabled{true};
    bool is_active{false}; // Highlighted when mode is armed

    bool contains(int32_t px, int32_t py) const noexcept {
        return px >= x && px < (x + w) && py >= y && py < (y + h);
    }
};

/**
 * @brief Master In-Game HUD subsystem for Ants Remake.
 */
class HUD {
public:
    // Virtual 640x480 screen layout constants
    static constexpr int32_t SCREEN_WIDTH      = 640;
    static constexpr int32_t SCREEN_HEIGHT     = 480;

    static constexpr int32_t PLAYFIELD_X       = 17;
    static constexpr int32_t PLAYFIELD_Y       = 22;
    static constexpr int32_t PLAYFIELD_WIDTH   = 441;
    static constexpr int32_t PLAYFIELD_HEIGHT  = 439;

    static constexpr int32_t RADAR_X           = 480;
    static constexpr int32_t RADAR_Y           = 22;
    static constexpr int32_t RADAR_WIDTH       = 160;
    static constexpr int32_t RADAR_HEIGHT      = 104;

    static constexpr int32_t CARD_X            = 480;
    static constexpr int32_t CARD_Y            = 126;
    static constexpr int32_t CARD_WIDTH        = 160;
    static constexpr int32_t CARD_HEIGHT       = 128;

    static constexpr int32_t HATCH_X           = 480;
    static constexpr int32_t HATCH_Y           = 254;
    static constexpr int32_t HATCH_WIDTH       = 160;
    static constexpr int32_t HATCH_HEIGHT      = 106;

    static constexpr int32_t ACTIONS_X         = 480;
    static constexpr int32_t ACTIONS_Y         = 360;
    static constexpr int32_t ACTIONS_WIDTH     = 160;
    static constexpr int32_t ACTIONS_HEIGHT    = 101;

    static constexpr int32_t BANNER_X          = 17;
    static constexpr int32_t BANNER_Y          = 461;
    static constexpr int32_t BANNER_WIDTH      = 623;
    static constexpr int32_t BANNER_HEIGHT     = 19;

    HUD();
    ~HUD() = default;

    void init(uint8_t local_player_id = 0);
    void reset();

    // Per-tick / per-frame update
    void update(const sim::WorldState& world, uint32_t delta_ticks);
    void poll_sim_events(sim::SimulationEngine& sim);

    // Rendering pipeline
    void render(IRenderer& renderer, const assets::AssetArchive& assets,
                const sim::WorldState& world, const Camera& camera);

    // Mouse & Keyboard Input Dispatch
    bool handle_mouse_down(int32_t x, int32_t y, uint8_t button,
                           sim::SimulationEngine& sim, Camera& camera);
    bool handle_mouse_up(int32_t x, int32_t y, uint8_t button,
                         sim::SimulationEngine& sim, Camera& camera);
    bool handle_mouse_motion(int32_t x, int32_t y,
                             sim::SimulationEngine& sim, Camera& camera);
    bool handle_key_down(int32_t key, sim::SimulationEngine& sim, Camera& camera);

    // Selection controls
    void select_ant(uint32_t ant_id) noexcept { selected_ant_id_ = ant_id; }
    void clear_selection() noexcept { selected_ant_id_ = 0; }
    uint32_t get_selected_ant_id() const noexcept { return selected_ant_id_; }

    // News banner
    void queue_news_message(const std::string& msg, uint32_t duration_ticks = 100, bool is_alarm = false);

    // Order mode
    sim::OrderType get_active_order_mode() const noexcept { return active_order_mode_; }
    void set_active_order_mode(sim::OrderType mode) noexcept;
    void cancel_order_mode() noexcept { set_active_order_mode(sim::OrderType::None); }

    // Local player identity
    uint8_t get_local_player_id() const noexcept { return local_player_id_; }
    void set_local_player_id(uint8_t id) noexcept { local_player_id_ = id; }

private:
    void render_top_bar(IRenderer& renderer, const assets::AssetArchive& assets, const sim::WorldState& world);
    void render_radar(IRenderer& renderer, const assets::AssetArchive& assets, const sim::WorldState& world, const Camera& camera);
    void render_selection_card(IRenderer& renderer, const assets::AssetArchive& assets, const sim::WorldState& world);
    void render_hatch_panel(IRenderer& renderer, const assets::AssetArchive& assets, const sim::WorldState& world);
    void render_action_buttons(IRenderer& renderer, const assets::AssetArchive& assets, const sim::WorldState& world);
    void render_news_banner(IRenderer& renderer, const assets::AssetArchive& assets);
    void render_marquee_box(IRenderer& renderer);

    void update_action_buttons_state(const sim::WorldState& world);
    void dispatch_targeted_order(int32_t world_x, int32_t world_y, sim::SimulationEngine& sim);
    void dispatch_smart_right_click(int32_t world_x, int32_t world_y, sim::SimulationEngine& sim);

    uint8_t local_player_id_{0};
    uint32_t selected_ant_id_{0};
    sim::OrderType active_order_mode_{sim::OrderType::None};

    // Marquee drag selection
    bool is_dragging_{false};
    int32_t drag_start_x_{0};
    int32_t drag_start_y_{0};
    int32_t drag_curr_x_{0};
    int32_t drag_curr_y_{0};

    // Hatch & Incubation
    UIButton hatch_button_{};
    uint32_t incubation_timer_ticks_{0};
    bool is_incubating_{false};

    // Action buttons
    std::array<UIButton, static_cast<size_t>(ActionButtonId::Count)> action_buttons_{};

    // News Flash FIFO queue
    std::deque<NewsBannerItem> news_queue_{};
    uint32_t alarm_blink_ticks_{0};

    // Minimap drag navigation state
    bool is_radar_dragging_{false};
};

} // namespace ants::app
```

---

## 4. C++ Header Blueprint: `include/ants_app/scorecard.hpp`

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <functional>

#include "ants_assets/asset_archive.hpp"
#include "ants_sim/match_stats.hpp"

namespace ants::app {

class IRenderer;

/**
 * @brief Results Scorecard Modal dialog (re_screen / Animation 25).
 * Displayed upon match conclusion (clock 0:00) with simulation freeze.
 */
class ScorecardModal {
public:
    // Layout coordinates from reverse engineered Animation 25
    static constexpr int32_t BANNER_X          = 140;
    static constexpr int32_t BANNER_Y          = 0;
    static constexpr int32_t TITLE_X           = 41;
    static constexpr int32_t TITLE_Y           = 55;
    static constexpr int32_t STATS_X           = 342;
    static constexpr int32_t STATS_Y           = 84;
    static constexpr int32_t WINNER_HDR_X      = 40;
    static constexpr int32_t WINNER_HDR_Y      = 195;
    static constexpr int32_t WINNER_BOX_X      = 40;
    static constexpr int32_t WINNER_BOX_Y      = 222;
    static constexpr int32_t WINNER_BOX_W      = 558;
    static constexpr int32_t WINNER_BOX_H      = 50;
    static constexpr int32_t OTHER_HDR_X       = 40;
    static constexpr int32_t OTHER_HDR_Y       = 280;
    static constexpr int32_t OTHER_BOX_X       = 40;
    static constexpr int32_t OTHER_BOX_Y       = 310;
    static constexpr int32_t OTHER_BOX_W       = 558;
    static constexpr int32_t OTHER_BOX_H       = 130;

    // 4 Statistic column X coordinates (aligned with newstats.bmp arrow tips)
    static constexpr int32_t COL_SCORE_X       = 496;
    static constexpr int32_t COL_LOST_X        = 536;
    static constexpr int32_t COL_KILLED_X      = 557;
    static constexpr int32_t COL_HATCHED_X     = 578;

    // Interactive button positions
    static constexpr int32_t OK_BTN_X          = 530;
    static constexpr int32_t OK_BTN_Y          = 448;
    static constexpr int32_t OK_BTN_W          = 46;
    static constexpr int32_t OK_BTN_H          = 20;

    static constexpr int32_t QUIT_BTN_X        = 420;
    static constexpr int32_t QUIT_BTN_Y        = 448;
    static constexpr int32_t QUIT_BTN_W        = 98;
    static constexpr int32_t QUIT_BTN_H        = 26;

    ScorecardModal();
    ~ScorecardModal() = default;

    void show(const sim::MatchResult& result, uint8_t local_player_id);
    void hide() noexcept { is_active_ = false; }
    bool is_open() const noexcept { return is_active_; }

    bool handle_mouse_down(int32_t x, int32_t y);
    bool handle_mouse_up(int32_t x, int32_t y);

    void render(IRenderer& renderer, const assets::AssetArchive& assets);

    // Audio routing query
    uint32_t get_audio_to_play() const noexcept { return audio_to_play_; }
    void clear_audio_to_play() noexcept { audio_to_play_ = 0; }

    // Action callbacks
    void set_on_replay(std::function<void()> cb) { on_replay_ = std::move(cb); }
    void set_on_quit(std::function<void()> cb) { on_quit_ = std::move(cb); }

private:
    struct PlayerEntry {
        uint8_t player_id{0};
        std::string name;
        int32_t score{0};
        uint32_t friendly_lost{0};
        uint32_t enemy_killed{0};
        uint32_t new_hatched{0};
        bool is_winner{false};
        bool is_allied{false};
    };

    bool is_active_{false};
    uint8_t local_player_id_{0};
    uint32_t audio_to_play_{0}; // Sound 56 (winner) vs Sound 41 (loser)
    bool ok_pressed_{false};
    bool quit_pressed_{false};

    PlayerEntry winner_entry_{};
    std::vector<PlayerEntry> other_entries_{};

    std::function<void()> on_replay_;
    std::function<void()> on_quit_;
};

} // namespace ants::app
```

---

## 5. C++ Implementation Blueprint: `src/ants_app/hud.cpp`

```cpp
#include "ants_app/hud.hpp"
#include "ants_app/renderer.hpp"

#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace ants::app {

namespace {

// Team color RGB palettes
constexpr assets::ColorRGBA TEAM_COLORS[4] = {
    {79, 87, 111, 255},   // 0: Black
    {119, 175, 239, 255}, // 1: Blue
    {251, 51, 91, 255},   // 2: Red
    {83, 147, 43, 255}    // 3: Green
};

const char* ANT_TYPE_NAMES[] = {
    "Worker Ant",
    "Bomber Ant",
    "Fire Ant",
    "Thief Ant",
    "Combat Ant",
    "Swimmer Ant"
};

} // anonymous namespace

HUD::HUD() {
    init(0);
}

void HUD::init(uint8_t local_player_id) {
    local_player_id_ = local_player_id;
    selected_ant_id_ = 0;
    active_order_mode_ = sim::OrderType::None;
    is_dragging_ = false;
    is_radar_dragging_ = false;
    incubation_timer_ticks_ = 0;
    is_incubating_ = false;
    news_queue_.clear();

    // Configure Hatch Button (492, 262)
    hatch_button_.x = 492;
    hatch_button_.y = 262;
    hatch_button_.w = 36;
    hatch_button_.h = 26;
    hatch_button_.sprite_up = 2683;    // buthatup.bmp
    hatch_button_.sprite_down = 2684;  // buthatd.bmp
    hatch_button_.sprite_label = 2682; // labhatch.bmp

    // Configure 7 Action Buttons at (484..636, 362..458)
    // 1. Move
    action_buttons_[0] = {490, 365, 45, 30, 2573, 2585, 2572, false, true, false}; // butmovu, butmovd, labmov
    // 2. Attack
    action_buttons_[1] = {540, 365, 45, 30, 2580, 2589, 2579, false, true, false}; // butattu, butattd, labatt
    // 3. Bomb
    action_buttons_[2] = {590, 365, 45, 30, 2581, 2590, 2582, false, false, false}; // butbomu, butbomd, labbom
    // 4. Fire
    action_buttons_[3] = {490, 400, 45, 30, 2584, 2584, 2583, false, false, false}; // butfireu, labfire
    // 5. Bridge
    action_buttons_[4] = {540, 400, 45, 30, 2576, 2587, 2575, false, false, false}; // butdipu, butdipd, labdib
    // 6. Thief
    action_buttons_[5] = {590, 400, 45, 30, 2577, 2588, 2578, false, false, false}; // butthfu, butthfd, labthf
    // 7. Cancel
    action_buttons_[6] = {540, 435, 45, 25, 2706, 2707, 2705, false, true, false}; // butcanu, butcand, labcan

    queue_news_message("Ants Remake", 200, false);
}

void HUD::reset() {
    init(local_player_id_);
}

void HUD::set_active_order_mode(sim::OrderType mode) noexcept {
    active_order_mode_ = mode;
    for (auto& btn : action_buttons_) {
        btn.is_active = false;
    }
    switch (mode) {
        case sim::OrderType::Move:              action_buttons_[0].is_active = true; break;
        case sim::OrderType::Attack:            action_buttons_[1].is_active = true; break;
        case sim::OrderType::PlantBomb:
        case sim::OrderType::DefuseBomb:        action_buttons_[2].is_active = true; break;
        case sim::OrderType::IgniteFire:
        case sim::OrderType::ExtinguishFire:    action_buttons_[3].is_active = true; break;
        case sim::OrderType::BuildBridge:       action_buttons_[4].is_active = true; break;
        case sim::OrderType::InfiltrateAnthill: action_buttons_[5].is_active = true; break;
        default: break;
    }
}

void HUD::update(const sim::WorldState& world, uint32_t delta_ticks) {
    // 1. Update news banner FIFO queue
    if (!news_queue_.empty()) {
        if (news_queue_.front().remaining_ticks <= delta_ticks) {
            news_queue_.pop_front();
        } else {
            news_queue_.front().remaining_ticks -= delta_ticks;
        }
    }

    // 2. Alarm siren blinking
    alarm_blink_ticks_ += delta_ticks;

    // 3. Incubation progress
    if (is_incubating_) {
        if (incubation_timer_ticks_ <= delta_ticks) {
            incubation_timer_ticks_ = 0;
            is_incubating_ = false;
        } else {
            incubation_timer_ticks_ -= delta_ticks;
        }
    }

    // 4. Update button contextual enabled status
    update_action_buttons_state(world);
}

void HUD::poll_sim_events(sim::SimulationEngine& sim) {
    auto news = sim.poll_news_events();
    for (const auto& ev : news) {
        if (ev.target_player == 255 || ev.target_player == local_player_id_) {
            bool is_thief_alarm = (ev.string_id == sim::StringID::ThiefAlarmWarning);
            queue_news_message(ev.message_text, 100, is_thief_alarm);
        }
    }
}

void HUD::queue_news_message(const std::string& msg, uint32_t duration_ticks, bool is_alarm) {
    news_queue_.push_back({msg, duration_ticks, is_alarm, 255});
    if (news_queue_.size() > 32) {
        news_queue_.pop_front();
    }
}

void HUD::update_action_buttons_state(const sim::WorldState& world) {
    // Check if selected ant still exists and is alive
    const sim::AntSnapshot* selected = nullptr;
    if (selected_ant_id_ != 0) {
        for (const auto& ant : world.ants) {
            if (ant.id == selected_ant_id_ && ant.player_id == local_player_id_) {
                selected = &ant;
                break;
            }
        }
    }

    if (!selected) {
        // No friendly unit selected: disable all action buttons except Cancel
        for (size_t i = 0; i < 6; ++i) action_buttons_[i].is_enabled = false;
        action_buttons_[6].is_enabled = true; // Cancel
        return;
    }

    // Move & Attack are universally enabled for all friendly units
    action_buttons_[0].is_enabled = true; // Move
    action_buttons_[1].is_enabled = true; // Attack

    // Class-specific abilities
    action_buttons_[2].is_enabled = (selected->type == sim::AntType::Bomber);
    action_buttons_[3].is_enabled = (selected->type == sim::AntType::Fire);
    action_buttons_[4].is_enabled = (selected->type == sim::AntType::Swimmer);
    action_buttons_[5].is_enabled = (selected->type == sim::AntType::Thief);
    action_buttons_[6].is_enabled = true; // Cancel

    // Hatch button check: cost 200 pts and > 0 eggs
    int32_t score = world.player_scores[local_player_id_];
    uint32_t eggs = world.player_eggs[local_player_id_];
    hatch_button_.is_enabled = (score >= 200 && eggs > 0);
}

// =========================================================================
// Rendering Subsystem
// =========================================================================

void HUD::render(IRenderer& renderer, const assets::AssetArchive& assets,
                 const sim::WorldState& world, const Camera& camera) {
    // 1. Playfield Frame Borders
    renderer.draw_sprite(2721, 0, 22);    // x0y22.bmp (17x458)
    renderer.draw_sprite(2719, 458, 35);  // x458y35.bmp (22x426)
    renderer.draw_sprite(2720, 458, 22);  // x458y22.bmp (182x13)

    // 2. Right Panel Modules
    render_radar(renderer, assets, world, camera);
    render_selection_card(renderer, assets, world);
    render_hatch_panel(renderer, assets, world);
    render_action_buttons(renderer, assets, world);

    // 3. Top & Bottom Frames
    render_top_bar(renderer, assets, world);
    render_news_banner(renderer, assets);

    // 4. Marquee Selection Box
    if (is_dragging_) {
        render_marquee_box(renderer);
    }
}

void HUD::render_top_bar(IRenderer& renderer, const assets::AssetArchive& assets, const sim::WorldState& world) {
    // Top border backdrop: Sprite 2709: x0y0.bmp (640x22)
    renderer.draw_sprite(2709, 0, 0);

    // Match Clock countdown
    uint32_t ms = world.match_time_remaining_ms;
    uint32_t mm = (ms / 1000) / 60;
    uint32_t ss = (ms / 1000) % 60;
    bool is_under_one_min = (ms < 60000 && ms > 0);

    // Render [MM:SS] digits: Sprite 2722 (dig0)..2731 (dig9), 2732 (digc)
    int32_t cx = 300, cy = 6;
    renderer.draw_sprite(2722 + (mm / 10), cx + 0, cy);
    renderer.draw_sprite(2722 + (mm % 10), cx + 8, cy);
    renderer.draw_sprite(2732, cx + 16, cy); // colon
    renderer.draw_sprite(2722 + (ss / 10), cx + 24, cy);
    renderer.draw_sprite(2722 + (ss % 10), cx + 32, cy);

    // Multi-Faction Live Scores
    // Left side: Team 0 & Team 1
    renderer.fill_rect(25, 6, 8, 8, TEAM_COLORS[0]);
    renderer.draw_text("Black: " + std::to_string(world.player_scores[0]), 36, 6, {255, 255, 255, 255});

    renderer.fill_rect(140, 6, 8, 8, TEAM_COLORS[1]);
    renderer.draw_text("Blue: " + std::to_string(world.player_scores[1]), 151, 6, {255, 255, 255, 255});

    // Right side: Team 2 & Team 3
    renderer.fill_rect(380, 6, 8, 8, TEAM_COLORS[2]);
    renderer.draw_text("Red: " + std::to_string(world.player_scores[2]), 391, 6, {255, 255, 255, 255});

    renderer.fill_rect(500, 6, 8, 8, TEAM_COLORS[3]);
    renderer.draw_text("Green: " + std::to_string(world.player_scores[3]), 511, 6, {255, 255, 255, 255});

    // Alliance grouping display
    for (uint8_t p = 0; p < 4; ++p) {
        uint8_t ally = world.player_alliances[p];
        if (ally < 4 && ally > p) {
            std::string ally_tag = "[" + std::to_string(p) + "+" + std::to_string(ally) + "]";
            renderer.draw_text(ally_tag, 255, 6, {255, 215, 0, 255});
        }
    }
}

void HUD::render_radar(IRenderer& renderer, const assets::AssetArchive& assets,
                       const sim::WorldState& world, const Camera& camera) {
    // Minimap panel background at (480, 22)
    renderer.draw_sprite(2713, 599, 35); // x599y35.bmp

    const int32_t rw = 110, rh = 90;
    const int32_t rx = 485, ry = 28;

    // Fill radar black / clay base
    renderer.fill_rect(rx, ry, rw, rh, {30, 25, 20, 255});
    renderer.draw_rect(rx, ry, rw, rh, {120, 100, 80, 255});

    if (world.width == 0 || world.height == 0) return;

    float scale_x = static_cast<float>(rw) / world.width;
    float scale_y = static_cast<float>(rh) / world.height;

    // Anthill base markers
    for (const auto& base : world.anthills) {
        int32_t bx = rx + static_cast<int32_t>(base.tile_x * scale_x);
        int32_t by = ry + static_cast<int32_t>(base.tile_y * scale_y);
        assets::ColorRGBA c = (base.team_id < 4) ? TEAM_COLORS[base.team_id] : assets::ColorRGBA{200, 200, 200, 255};
        renderer.fill_rect(bx - 2, by - 2, 5, 5, c);
    }

    // Active live ants
    for (const auto& ant : world.ants) {
        if (ant.hp == 0 || ant.is_drowning) continue;
        int32_t ax = rx + static_cast<int32_t>(ant.tile_x * scale_x);
        int32_t ay = ry + static_cast<int32_t>(ant.tile_y * scale_y);
        assets::ColorRGBA c = (ant.player_id < 4) ? TEAM_COLORS[ant.player_id] : assets::ColorRGBA{255, 255, 255, 255};
        renderer.fill_rect(ax, ay, 2, 2, c);

        if (ant.id == selected_ant_id_) {
            // Outline selected unit
            renderer.draw_rect(ax - 1, ay - 1, 4, 4, {255, 255, 255, 255});
        }
    }

    // Camera frustum wireframe box
    float cam_tile_x = static_cast<float>(camera.world_x) / 32.0f;
    float cam_tile_y = static_cast<float>(camera.world_y) / 32.0f;
    float cam_tile_w = static_cast<float>(camera.viewport_w) / 32.0f;
    float cam_tile_h = static_cast<float>(camera.viewport_h) / 32.0f;

    int32_t fx = rx + static_cast<int32_t>(cam_tile_x * scale_x);
    int32_t fy = ry + static_cast<int32_t>(cam_tile_y * scale_y);
    int32_t fw = static_cast<int32_t>(cam_tile_w * scale_x);
    int32_t fh = static_cast<int32_t>(cam_tile_h * scale_y);

    renderer.draw_rect(fx, fy, std::max(4, fw), std::max(4, fh), {255, 255, 255, 255});
}

void HUD::render_selection_card(IRenderer& renderer, const assets::AssetArchive& assets, const sim::WorldState& world) {
    // Card panel background: Sprite 2718: x480y126.bmp (160x128)
    renderer.draw_sprite(2718, CARD_X, CARD_Y);

    const sim::AntSnapshot* sel = nullptr;
    if (selected_ant_id_ != 0) {
        for (const auto& a : world.ants) {
            if (a.id == selected_ant_id_) { sel = &a; break; }
        }
    }

    if (!sel) {
        renderer.draw_text("No Selection", CARD_X + 40, CARD_Y + 50, {160, 160, 160, 255});
        return;
    }

    // Title header: wtype.bmp (2711) at (488, 130)
    renderer.draw_sprite(2711, 488, 130);
    uint8_t type_idx = static_cast<uint8_t>(sel->type);
    if (type_idx < 6) {
        renderer.draw_text(ANT_TYPE_NAMES[type_idx], 505, 133, {255, 255, 255, 255});
    }

    // Portrait frame at (525, 145)
    uint32_t portrait_sprite = 1481; // Worker agst301
    switch (sel->type) {
        case sim::AntType::Worker:  portrait_sprite = 1481; break;
        case sim::AntType::Bomber:  portrait_sprite = 1332; break;
        case sim::AntType::Fire:    portrait_sprite = 968;  break;
        case sim::AntType::Combat:  portrait_sprite = 1807; break;
        case sim::AntType::Swimmer: portrait_sprite = 2014; break;
        case sim::AntType::Thief:   portrait_sprite = 2470; break;
    }
    renderer.draw_sprite(portrait_sprite, 525, 145);

    // Lunchbox / Carrying icon
    if (sel->is_holding || sel->carried_points > 0) {
        renderer.draw_sprite(2694, 592, 145); // lunchicon.bmp
        if (sel->carried_points > 0) {
            renderer.draw_text("+" + std::to_string(sel->carried_points), 592, 185, {255, 215, 0, 255});
        }
    }

    // Health Bar at (496, 192)
    renderer.fill_rect(496, 192, 128, 8, {40, 40, 40, 255});
    float frac = std::clamp(static_cast<float>(sel->hp) / static_cast<float>(sel->max_hp), 0.0f, 1.0f);
    int32_t fill_w = static_cast<int32_t>(frac * 128.0f);

    assets::ColorRGBA hp_color = {0, 200, 0, 255}; // Green
    if (sel->hp <= 3) hp_color = {220, 30, 30, 255}; // Red
    else if (sel->hp <= 7) hp_color = {220, 200, 0, 255}; // Yellow

    if (fill_w > 0) renderer.fill_rect(496, 192, fill_w, 8, hp_color);
    renderer.draw_rect(496, 192, 128, 8, {100, 100, 100, 255});

    // Action status: wstatus.bmp (2710) at (488, 206)
    renderer.draw_sprite(2710, 488, 206);
    std::string status_str = "Idle";
    if (sel->is_drowning) status_str = "Drowning";
    else if (sel->is_airborne) status_str = "Airborne";
    else if (sel->is_stunned) status_str = "Stunned";
    else if (sel->is_underground) status_str = "In Base";
    else if (sel->is_swimming) status_str = "Swimming";
    else if (sel->anim_state == 2) status_str = "Moving";
    else if (sel->anim_state == 3) status_str = "Attacking";
    else if (sel->is_holding) status_str = "Carrying Food";

    renderer.draw_text(status_str, 510, 209, {255, 255, 255, 255});
}

void HUD::render_hatch_panel(IRenderer& renderer, const assets::AssetArchive& assets, const sim::WorldState& world) {
    // Backing trim
    renderer.draw_sprite(2717, 480, 266); // x480y266.bmp
    renderer.draw_sprite(2714, 521, 254); // x521y254.bmp

    // Hatch button
    uint32_t btn_sprite = hatch_button_.is_pressed ? hatch_button_.sprite_down : hatch_button_.sprite_up;
    renderer.draw_sprite(btn_sprite, hatch_button_.x, hatch_button_.y);
    renderer.draw_sprite(hatch_button_.sprite_label, hatch_button_.x + 2, hatch_button_.y + 6);

    // Cost text: 200 pts
    assets::ColorRGBA cost_color = hatch_button_.is_enabled ? assets::ColorRGBA{255, 215, 0, 255} : assets::ColorRGBA{130, 130, 130, 255};
    renderer.draw_text("200 pts", 540, 270, cost_color);

    // Egg Pile visualization
    uint32_t eggs = world.player_eggs[local_player_id_];
    if (eggs >= 8) {
        renderer.draw_sprite(554, 495, 305); // eggs.bmp
    } else if (eggs >= 5) {
        renderer.draw_sprite(555, 495, 305); // eggsa.bmp
    } else if (eggs >= 2) {
        renderer.draw_sprite(556, 495, 305); // eggsb.bmp
    } else if (eggs == 1) {
        renderer.draw_sprite(2693, 530, 315); // egg.bmp
    }

    // Numerical egg count
    renderer.draw_text("Eggs: " + std::to_string(eggs), 560, 335, {255, 255, 255, 255});
}

void HUD::render_action_buttons(IRenderer& renderer, const assets::AssetArchive& assets, const sim::WorldState& world) {
    // 7 Action buttons
    for (size_t i = 0; i < action_buttons_.size(); ++i) {
        const auto& btn = action_buttons_[i];
        uint32_t spr = (btn.is_pressed || btn.is_active) ? btn.sprite_down : btn.sprite_up;
        renderer.draw_sprite(spr, btn.x, btn.y);
        if (btn.sprite_label != 0) {
            renderer.draw_sprite(btn.sprite_label, btn.x + 4, btn.y + 6);
        }

        // Active selection highlight border
        if (btn.is_active) {
            renderer.draw_rect(btn.x - 1, btn.y - 1, btn.w + 2, btn.h + 2, {255, 215, 0, 255});
        }
    }
}

void HUD::render_news_banner(IRenderer& renderer, const assets::AssetArchive& assets) {
    // Bottom banner background: Sprite 2708: x17y461.bmp (623x19)
    renderer.draw_sprite(2708, BANNER_X, BANNER_Y);

    if (news_queue_.empty()) return;

    const auto& item = news_queue_.front();
    assets::ColorRGBA text_color = {255, 255, 255, 255};

    if (item.is_alarm) {
        // Blinking red/yellow for base alarm
        bool blink = (alarm_blink_ticks_ / 4) % 2 == 0;
        text_color = blink ? assets::ColorRGBA{255, 50, 50, 255} : assets::ColorRGBA{255, 255, 50, 255};
    }

    renderer.draw_text(item.text, BANNER_X + 20, BANNER_Y + 5, text_color);
}

void HUD::render_marquee_box(IRenderer& renderer) {
    int32_t x1 = std::min(drag_start_x_, drag_curr_x_);
    int32_t y1 = std::min(drag_start_y_, drag_curr_y_);
    int32_t x2 = std::max(drag_start_x_, drag_curr_x_);
    int32_t y2 = std::max(drag_start_y_, drag_curr_y_);

    // Clamp box to playfield boundary
    x1 = std::max(PLAYFIELD_X, x1);
    y1 = std::max(PLAYFIELD_Y, y1);
    x2 = std::min(PLAYFIELD_X + PLAYFIELD_WIDTH, x2);
    y2 = std::min(PLAYFIELD_Y + PLAYFIELD_HEIGHT, y2);

    int32_t w = x2 - x1;
    int32_t h = y2 - y1;

    renderer.draw_rect(x1, y1, w, h, {255, 255, 255, 255});
}

// =========================================================================
// Input Dispatcher
// =========================================================================

bool HUD::handle_mouse_down(int32_t x, int32_t y, uint8_t button,
                            sim::SimulationEngine& sim, Camera& camera) {
    // 1. Check Hatch Button click
    if (hatch_button_.contains(x, y)) {
        if (hatch_button_.is_enabled) {
            hatch_button_.is_pressed = true;
            sim.hatch_ant(local_player_id_, sim::AntType::Worker);
            is_incubating_ = true;
            incubation_timer_ticks_ = 60; // 3 seconds
        }
        return true;
    }

    // 2. Check Action Buttons
    for (size_t i = 0; i < action_buttons_.size(); ++i) {
        if (action_buttons_[i].contains(x, y)) {
            if (action_buttons_[i].is_enabled) {
                action_buttons_[i].is_pressed = true;
                switch (static_cast<ActionButtonId>(i)) {
                    case ActionButtonId::Move:   set_active_order_mode(sim::OrderType::Move); break;
                    case ActionButtonId::Attack: set_active_order_mode(sim::OrderType::Attack); break;
                    case ActionButtonId::Bomb:   set_active_order_mode(sim::OrderType::PlantBomb); break;
                    case ActionButtonId::Fire:   set_active_order_mode(sim::OrderType::IgniteFire); break;
                    case ActionButtonId::Bridge: set_active_order_mode(sim::OrderType::BuildBridge); break;
                    case ActionButtonId::Thief:  set_active_order_mode(sim::OrderType::InfiltrateAnthill); break;
                    case ActionButtonId::Cancel: cancel_order_mode(); break;
                    default: break;
                }
            }
            return true;
        }
    }

    // 3. Check Minimap Radar click
    const int32_t rw = 110, rh = 90;
    const int32_t rx = 485, ry = 28;
    if (x >= rx && x < (rx + rw) && y >= ry && y < (ry + rh)) {
        is_radar_dragging_ = true;
        const auto& world = sim.get_world_state();
        if (world.width > 0 && world.height > 0) {
            int32_t tile_x = static_cast<int32_t>((static_cast<float>(x - rx) / rw) * world.width);
            int32_t tile_y = static_cast<int32_t>((static_cast<float>(y - ry) / rh) * world.height);
            tile_x = std::max(0, std::min(static_cast<int32_t>(world.width - 1), tile_x));
            tile_y = std::max(0, std::min(static_cast<int32_t>(world.height - 1), tile_y));
            camera.center_on(tile_x * 32, tile_y * 32, world.width * 32, world.height * 32);
        }
        return true;
    }

    // 4. Playfield Interactions
    if (x >= PLAYFIELD_X && x < (PLAYFIELD_X + PLAYFIELD_WIDTH) &&
        y >= PLAYFIELD_Y && y < (PLAYFIELD_Y + PLAYFIELD_HEIGHT)) {

        int32_t world_x = camera.world_x + (x - PLAYFIELD_X);
        int32_t world_y = camera.world_y + (y - PLAYFIELD_Y);

        if (button == 1) { // Left-click
            if (active_order_mode_ != sim::OrderType::None) {
                dispatch_targeted_order(world_x, world_y, sim);
                cancel_order_mode();
                return true;
            }

            // Start possible drag / click selection
            is_dragging_ = true;
            drag_start_x_ = x;
            drag_start_y_ = y;
            drag_curr_x_ = x;
            drag_curr_y_ = y;
            return true;
        } else if (button == 3) { // Right-click (Contextual smart order)
            if (active_order_mode_ != sim::OrderType::None) {
                cancel_order_mode();
            } else {
                dispatch_smart_right_click(world_x, world_y, sim);
            }
            return true;
        }
    }

    return false;
}

bool HUD::handle_mouse_up(int32_t x, int32_t y, uint8_t button,
                          sim::SimulationEngine& sim, Camera& camera) {
    hatch_button_.is_pressed = false;
    for (auto& btn : action_buttons_) btn.is_pressed = false;
    is_radar_dragging_ = false;

    if (is_dragging_ && button == 1) {
        is_dragging_ = false;
        int32_t dx = std::abs(drag_curr_x_ - drag_start_x_);
        int32_t dy = std::abs(drag_curr_y_ - drag_start_y_);

        const auto& world = sim.get_world_state();

        if (dx < 6 && dy < 6) {
            // Single Click Unit Selection
            int32_t world_x = camera.world_x + (drag_start_x_ - PLAYFIELD_X);
            int32_t world_y = camera.world_y + (drag_start_y_ - PLAYFIELD_Y);

            uint32_t hit_ant_id = 0;
            for (const auto& ant : world.ants) {
                if (ant.hp == 0 || ant.is_drowning) continue;
                int32_t ax = ant.tile_x * 32 + 16;
                int32_t ay = ant.tile_y * 32 + 16;
                if (std::abs(ax - world_x) <= 16 && std::abs(ay - world_y) <= 16) {
                    hit_ant_id = ant.id;
                    break;
                }
            }
            selected_ant_id_ = hit_ant_id;
        } else {
            // Marquee Box Selection
            int32_t x1 = camera.world_x + (std::min(drag_start_x_, drag_curr_x_) - PLAYFIELD_X);
            int32_t y1 = camera.world_y + (std::min(drag_start_y_, drag_curr_y_) - PLAYFIELD_Y);
            int32_t x2 = camera.world_x + (std::max(drag_start_x_, drag_curr_x_) - PLAYFIELD_X);
            int32_t y2 = camera.world_y + (std::max(drag_start_y_, drag_curr_y_) - PLAYFIELD_Y);

            uint32_t first_friendly = 0;
            for (const auto& ant : world.ants) {
                if (ant.hp == 0 || ant.is_drowning) continue;
                if (ant.player_id == local_player_id_) {
                    int32_t ax = ant.tile_x * 32 + 16;
                    int32_t ay = ant.tile_y * 32 + 16;
                    if (ax >= x1 && ax <= x2 && ay >= y1 && ay <= y2) {
                        first_friendly = ant.id;
                        break;
                    }
                }
            }
            selected_ant_id_ = first_friendly;
        }
        return true;
    }

    return false;
}

bool HUD::handle_mouse_motion(int32_t x, int32_t y,
                              sim::SimulationEngine& sim, Camera& camera) {
    if (is_dragging_) {
        drag_curr_x_ = x;
        drag_curr_y_ = y;
        return true;
    }

    if (is_radar_dragging_) {
        const int32_t rw = 110, rh = 90;
        const int32_t rx = 485, ry = 28;
        const auto& world = sim.get_world_state();
        if (world.width > 0 && world.height > 0) {
            int32_t tile_x = static_cast<int32_t>((static_cast<float>(x - rx) / rw) * world.width);
            int32_t tile_y = static_cast<int32_t>((static_cast<float>(y - ry) / rh) * world.height);
            tile_x = std::max(0, std::min(static_cast<int32_t>(world.width - 1), tile_x));
            tile_y = std::max(0, std::min(static_cast<int32_t>(world.height - 1), tile_y));
            camera.center_on(tile_x * 32, tile_y * 32, world.width * 32, world.height * 32);
        }
        return true;
    }

    return false;
}

bool HUD::handle_key_down(int32_t key, sim::SimulationEngine& sim, Camera& camera) {
    switch (key) {
        case 'm': case 'M': set_active_order_mode(sim::OrderType::Move); return true;
        case 'a': case 'A': set_active_order_mode(sim::OrderType::Attack); return true;
        case 'b': case 'B': set_active_order_mode(sim::OrderType::PlantBomb); return true;
        case 'f': case 'F': set_active_order_mode(sim::OrderType::IgniteFire); return true;
        case 's': case 'S': set_active_order_mode(sim::OrderType::BuildBridge); return true;
        case 't': case 'T': set_active_order_mode(sim::OrderType::InfiltrateAnthill); return true;
        case 'h': case 'H':
            if (hatch_button_.is_enabled) {
                sim.hatch_ant(local_player_id_, sim::AntType::Worker);
                is_incubating_ = true;
                incubation_timer_ticks_ = 60;
            }
            return true;
        case 27: // Escape
        case 'c': case 'C':
            cancel_order_mode();
            clear_selection();
            return true;
        case ' ': // Space: Center on selected ant
            if (selected_ant_id_ != 0) {
                const auto& world = sim.get_world_state();
                for (const auto& a : world.ants) {
                    if (a.id == selected_ant_id_) {
                        camera.center_on(a.tile_x * 32 + 16, a.tile_y * 32 + 16,
                                         world.width * 32, world.height * 32);
                        break;
                    }
                }
            }
            return true;
        default: break;
    }
    return false;
}

void HUD::dispatch_targeted_order(int32_t world_x, int32_t world_y, sim::SimulationEngine& sim) {
    if (selected_ant_id_ == 0) return;

    int32_t target_tile_x = world_x / 32;
    int32_t target_tile_y = world_y / 32;

    sim::AntOrder order;
    order.ant_id = selected_ant_id_;
    order.type = active_order_mode_;
    order.target_x = target_tile_x;
    order.target_y = target_tile_y;

    sim.issue_order(order);
}

void HUD::dispatch_smart_right_click(int32_t world_x, int32_t world_y, sim::SimulationEngine& sim) {
    if (selected_ant_id_ == 0) return;

    int32_t target_tile_x = world_x / 32;
    int32_t target_tile_y = world_y / 32;

    const auto& world = sim.get_world_state();
    const sim::AntSnapshot* sel = nullptr;
    for (const auto& a : world.ants) {
        if (a.id == selected_ant_id_) { sel = &a; break; }
    }
    if (!sel || sel->player_id != local_player_id_) return;

    sim::AntOrder order;
    order.ant_id = selected_ant_id_;
    order.target_x = target_tile_x;
    order.target_y = target_tile_y;

    // 1. Check if clicked on enemy ant
    for (const auto& target_ant : world.ants) {
        if (target_ant.hp == 0 || target_ant.is_drowning) continue;
        if (target_ant.player_id != local_player_id_) {
            if (target_ant.tile_x == target_tile_x && target_ant.tile_y == target_tile_y) {
                order.type = sim::OrderType::Attack;
                order.target_entity_id = static_cast<int32_t>(target_ant.id);
                sim.issue_order(order);
                return;
            }
        }
    }

    // 2. Check if clicked on anthill base
    for (const auto& base : world.anthills) {
        if (base.tile_x == target_tile_x && base.tile_y == target_tile_y) {
            if (base.team_id == local_player_id_) {
                order.type = sim::OrderType::ReturnToBase;
                sim.issue_order(order);
                return;
            } else {
                if (sel->type == sim::AntType::Thief) {
                    order.type = sim::OrderType::InfiltrateAnthill;
                    sim.issue_order(order);
                    return;
                } else {
                    // Propose alliance to target player
                    sim.propose_alliance(local_player_id_, base.team_id);
                    return;
                }
            }
        }
    }

    // 3. Check for bomb on target tile (defusal)
    if (sel->type == sim::AntType::Bomber && sim.has_bomb_at({target_tile_x, target_tile_y})) {
        order.type = sim::OrderType::DefuseBomb;
        sim.issue_order(order);
        return;
    }

    // 4. Check for fire on target tile (extinguish)
    if (sel->type == sim::AntType::Fire && sim.has_fire_at({target_tile_x, target_tile_y})) {
        order.type = sim::OrderType::ExtinguishFire;
        sim.issue_order(order);
        return;
    }

    // 5. Default right click: Move order
    order.type = sim::OrderType::Move;
    sim.issue_order(order);
}

} // namespace ants::app
```

---

## 6. C++ Implementation Blueprint: `src/ants_app/scorecard.cpp`

```cpp
#include "ants_app/scorecard.hpp"
#include "ants_app/renderer.hpp"

#include <algorithm>

namespace ants::app {

namespace {

const char* PLAYER_NAMES[4] = {
    "Black Team",
    "Blue Team",
    "Red Team",
    "Green Team"
};

constexpr assets::ColorRGBA TEAM_COLORS[4] = {
    {79, 87, 111, 255},   // 0: Black
    {119, 175, 239, 255}, // 1: Blue
    {251, 51, 91, 255},   // 2: Red
    {83, 147, 43, 255}    // 3: Green
};

} // anonymous namespace

ScorecardModal::ScorecardModal() = default;

void ScorecardModal::show(const sim::MatchResult& result, uint8_t local_player_id) {
    is_active_ = true;
    local_player_id_ = local_player_id;
    ok_pressed_ = false;
    quit_pressed_ = false;

    // Check if local player won
    bool local_won = result.is_winner(local_player_id);
    audio_to_play_ = local_won ? sim::SoundID::VictoryFanfare : sim::SoundID::PlayerDefeat;

    // Find winner entry
    uint8_t winner_id = result.winning_players.empty() ? 0 : result.winning_players[0];
    winner_entry_.player_id = winner_id;
    winner_entry_.name = std::string(PLAYER_NAMES[winner_id]) + " (Winner)";
    winner_entry_.score = result.final_scores[winner_id];
    winner_entry_.friendly_lost = result.stats[winner_id].friendly_lost;
    winner_entry_.enemy_killed = result.stats[winner_id].enemy_killed;
    winner_entry_.new_hatched = result.stats[winner_id].new_hatched;
    winner_entry_.is_winner = true;

    // Assemble other player entries sorted by score descending
    other_entries_.clear();
    for (uint8_t p = 0; p < 4; ++p) {
        if (p == winner_id) continue;
        PlayerEntry pe;
        pe.player_id = p;
        pe.name = PLAYER_NAMES[p];
        pe.score = result.final_scores[p];
        pe.friendly_lost = result.stats[p].friendly_lost;
        pe.enemy_killed = result.stats[p].enemy_killed;
        pe.new_hatched = result.stats[p].new_hatched;
        pe.is_winner = false;
        other_entries_.push_back(pe);
    }

    std::sort(other_entries_.begin(), other_entries_.end(), [](const PlayerEntry& a, const PlayerEntry& b) {
        return a.score > b.score;
    });
}

bool ScorecardModal::handle_mouse_down(int32_t x, int32_t y) {
    if (!is_active_) return false;

    // Test OK button
    if (x >= OK_BTN_X && x < (OK_BTN_X + OK_BTN_W) &&
        y >= OK_BTN_Y && y < (OK_BTN_Y + OK_BTN_H)) {
        ok_pressed_ = true;
        return true;
    }

    // Test Quit button
    if (x >= QUIT_BTN_X && x < (QUIT_BTN_X + QUIT_BTN_W) &&
        y >= QUIT_BTN_Y && y < (QUIT_BTN_Y + QUIT_BTN_H)) {
        quit_pressed_ = true;
        return true;
    }

    return true; // Modal consumes all click events
}

bool ScorecardModal::handle_mouse_up(int32_t x, int32_t y) {
    if (!is_active_) return false;

    if (ok_pressed_) {
        ok_pressed_ = false;
        if (x >= OK_BTN_X && x < (OK_BTN_X + OK_BTN_W) &&
            y >= OK_BTN_Y && y < (OK_BTN_Y + OK_BTN_H)) {
            if (on_replay_) on_replay_();
            return true;
        }
    }

    if (quit_pressed_) {
        quit_pressed_ = false;
        if (x >= QUIT_BTN_X && x < (QUIT_BTN_X + QUIT_BTN_W) &&
            y >= QUIT_BTN_Y && y < (QUIT_BTN_Y + QUIT_BTN_H)) {
            if (on_quit_) on_quit_();
            return true;
        }
    }

    return true;
}

void ScorecardModal::render(IRenderer& renderer, const assets::AssetArchive& assets) {
    if (!is_active_) return;

    // 1. Full-screen tiled clay backdrop: Sprite 2: dclay96.bmp (96x96)
    for (int32_t y = 0; y < 480; y += 96) {
        for (int32_t x = 0; x < 640; x += 96) {
            renderer.draw_sprite(2, x, y);
        }
    }

    // 2. Beveled Outer Frame Border Trim
    // Top, left, right, bottom border strips
    for (int32_t x = 0; x < 640; x += 96) {
        renderer.draw_sprite(10, x, 0);   // dfram296.bmp
        renderer.draw_sprite(3, x, 464);  // dfram796.bmp
    }
    for (int32_t y = 0; y < 480; y += 96) {
        renderer.draw_sprite(7, 0, y);    // dfram496.bmp
        renderer.draw_sprite(6, 624, y);  // dfram596.bmp
    }

    // 3. Top Banner Header: Sprite 99: resbanr.bmp (340x34) at (140, 0)
    renderer.draw_sprite(99, BANNER_X, BANNER_Y);

    // 4. Title Art: Sprite 98: yoscore.bmp (302x127) at (41, 55)
    renderer.draw_sprite(98, TITLE_X, TITLE_Y);

    // 5. Stats Header: Sprite 97: newstats.bmp (259x133) at (342, 84)
    renderer.draw_sprite(97, STATS_X, STATS_Y);

    // 6. Winner Section
    renderer.draw_sprite(96, WINNER_HDR_X, WINNER_HDR_Y); // winnr.bmp (117x19)

    // Winner Box: Sprite 93: bg50x100.bmp tiled across 558x50 at (40, 222)
    for (int32_t bx = WINNER_BOX_X; bx < (WINNER_BOX_X + WINNER_BOX_W); bx += 100) {
        renderer.draw_sprite(93, bx, WINNER_BOX_Y);
    }

    // Winner Row Data
    int32_t wy = WINNER_BOX_Y + 16;
    renderer.fill_rect(WINNER_BOX_X + 16, wy + 2, 10, 10, TEAM_COLORS[winner_entry_.player_id]);
    renderer.draw_text(winner_entry_.name, WINNER_BOX_X + 34, wy + 2, {255, 255, 255, 255});

    // 4 Columns aligned with newstats.bmp arrow tips
    renderer.draw_text(std::to_string(winner_entry_.score), COL_SCORE_X, wy, {255, 215, 0, 255});
    renderer.draw_text(std::to_string(winner_entry_.friendly_lost), COL_LOST_X, wy, {255, 255, 255, 255});
    renderer.draw_text(std::to_string(winner_entry_.enemy_killed), COL_KILLED_X, wy, {255, 255, 255, 255});
    renderer.draw_text(std::to_string(winner_entry_.new_hatched), COL_HATCHED_X, wy, {255, 255, 255, 255});

    // 7. Other Players Section
    renderer.draw_sprite(95, OTHER_HDR_X, OTHER_HDR_Y); // otherp.bmp (203x25)

    // Other Players Box: Sprite 94: efrbg100.bmp tiled across 558x130 at (40, 310)
    for (int32_t oy = OTHER_BOX_Y; oy < (OTHER_BOX_Y + OTHER_BOX_H); oy += 100) {
        for (int32_t ox = OTHER_BOX_X; ox < (OTHER_BOX_X + OTHER_BOX_W); ox += 100) {
            renderer.draw_sprite(94, ox, oy);
        }
    }

    // Other Player Rows
    int32_t py = OTHER_BOX_Y + 12;
    for (const auto& pe : other_entries_) {
        renderer.fill_rect(OTHER_BOX_X + 16, py + 2, 8, 8, TEAM_COLORS[pe.player_id]);
        renderer.draw_text(pe.name, OTHER_BOX_X + 34, py + 2, {220, 220, 220, 255});

        renderer.draw_text(std::to_string(pe.score), COL_SCORE_X, py, {255, 255, 255, 255});
        renderer.draw_text(std::to_string(pe.friendly_lost), COL_LOST_X, py, {200, 200, 200, 255});
        renderer.draw_text(std::to_string(pe.enemy_killed), COL_KILLED_X, py, {200, 200, 200, 255});
        renderer.draw_text(std::to_string(pe.new_hatched), COL_HATCHED_X, py, {200, 200, 200, 255});

        py += 35;
    }

    // 8. Action Buttons
    // OK Button: Sprite 74: dbutoku.bmp (46x20)
    renderer.draw_sprite(74, OK_BTN_X, OK_BTN_Y);

    // Quit / Return Button: Sprite 70: breturn1.bmp (98x26)
    renderer.draw_sprite(70, QUIT_BTN_X, QUIT_BTN_Y);
}

} // namespace ants::app
```

---

## 7. Zero-Dependency Built-In Crisp Bitmap Font Blueprint

To ensure 100% headless testability, CI reliability, and zero external file dependencies on `.ttf` files, the renderer implements a high-performance 8×8 ASCII bitmap font generator:

```cpp
namespace ants::app {

// 8x8 bitmap font character generator table (ASCII 32..126)
// Each character is 8 bytes, 1 bit per pixel.
extern const uint8_t FONT_8X8_BASIC[95][8];

inline void draw_bitmap_char(IRenderer& r, char ch, int32_t x, int32_t y, assets::ColorRGBA color) {
    if (ch < 32 || ch > 126) ch = '?';
    const uint8_t* glyph = FONT_8X8_BASIC[ch - 32];
    for (int row = 0; row < 8; ++row) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; ++col) {
            if (bits & (0x80 >> col)) {
                r.fill_rect(x + col, y + row, 1, 1, color);
            }
        }
    }
}

inline void draw_bitmap_string(IRenderer& r, const std::string& str, int32_t x, int32_t y, assets::ColorRGBA color) {
    int32_t cur_x = x;
    for (char c : str) {
        if (c == '\n') {
            cur_x = x;
            y += 10;
            continue;
        }
        draw_bitmap_char(r, c, cur_x, y, color);
        cur_x += 8;
    }
}

} // namespace ants::app
```

---

## 8. Integration With Main Application Loop

```cpp
// Typical dispatch in ants-app main loop:
void Application::handle_events() {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) is_running_ = false;

        // Coordinate scaling: convert physical mouse to logical 640x480
        if (ev.type == SDL_MOUSEBUTTONDOWN || ev.type == SDL_MOUSEBUTTONUP || ev.type == SDL_MOUSEMOTION) {
            Vec2i log_pt = scaler_.screen_to_logical(ev.button.x, ev.button.y);

            // If Scorecard Modal is open, it consumes all mouse events exclusively
            if (scorecard_modal_.is_open()) {
                if (ev.type == SDL_MOUSEBUTTONDOWN) scorecard_modal_.handle_mouse_down(log_pt.x, log_pt.y);
                else if (ev.type == SDL_MOUSEBUTTONUP) scorecard_modal_.handle_mouse_up(log_pt.x, log_pt.y);
                continue;
            }

            // Normal HUD & Playfield dispatch
            if (ev.type == SDL_MOUSEBUTTONDOWN) {
                hud_.handle_mouse_down(log_pt.x, log_pt.y, ev.button.button, sim_, camera_);
            } else if (ev.type == SDL_MOUSEBUTTONUP) {
                hud_.handle_mouse_up(log_pt.x, log_pt.y, ev.button.button, sim_, camera_);
            } else if (ev.type == SDL_MOUSEMOTION) {
                Vec2i motion_pt = scaler_.screen_to_logical(ev.motion.x, ev.motion.y);
                hud_.handle_mouse_motion(motion_pt.x, motion_pt.y, sim_, camera_);
            }
        } else if (ev.type == SDL_KEYDOWN) {
            if (!scorecard_modal_.is_open()) {
                hud_.handle_key_down(ev.key.keysym.sym, sim_, camera_);
            }
        }
    }
}
```
