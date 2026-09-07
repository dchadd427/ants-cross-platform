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
#include "ants_app/renderer.hpp"

namespace ants::app {

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
                const sim::WorldState& world, const ViewportCamera& camera);

    // Mouse & Keyboard Input Dispatch
    bool handle_mouse_down(int32_t x, int32_t y, uint8_t button,
                           sim::SimulationEngine& sim, ViewportCamera& camera);
    bool handle_mouse_up(int32_t x, int32_t y, uint8_t button,
                         sim::SimulationEngine& sim, ViewportCamera& camera);
    bool handle_mouse_motion(int32_t x, int32_t y,
                             sim::SimulationEngine& sim, ViewportCamera& camera);
    bool handle_key_down(int32_t key, sim::SimulationEngine& sim, ViewportCamera& camera);

    // Selection controls
    void select_ant(uint32_t ant_id);
    void select_base(int32_t team_id) noexcept;
    void clear_selection() noexcept;
    uint32_t get_selected_ant_id() const noexcept { return selected_ant_id_; }
    const std::vector<uint32_t>& get_selected_ant_ids() const noexcept { return selected_ant_ids_; }
    int32_t get_selected_base_team_id() const noexcept { return selected_base_team_id_; }
    bool is_ant_selected(uint32_t id) const noexcept;
    void select_all_friendly(const sim::WorldState& world);
    void select_ants_in_rect(int32_t x1, int32_t y1, int32_t x2, int32_t y2, const sim::WorldState& world);

    // News banner
    void queue_news_message(const std::string& msg, uint32_t duration_ticks = 100, bool is_alarm = false);

    // Order mode
    sim::OrderType get_active_order_mode() const noexcept { return active_order_mode_; }
    void set_active_order_mode(sim::OrderType mode) noexcept;
    void cancel_order_mode() noexcept { set_active_order_mode(sim::OrderType::None); }

    // Local player identity
    uint8_t get_local_player_id() const noexcept { return local_player_id_; }
    void set_local_player_id(uint8_t id) noexcept { local_player_id_ = id; }

    // Dialog and Modal overlays
    void open_quit_dialog() noexcept { show_quit_dialog_ = true; }
    void close_quit_dialog() noexcept { show_quit_dialog_ = false; }
    bool is_quit_dialog_open() const noexcept { return show_quit_dialog_; }
    void set_on_quit(std::function<void()> cb) { on_quit_ = std::move(cb); }

    void open_quick_help() noexcept { show_quick_help_ = true; }
    void close_quick_help() noexcept { show_quick_help_ = false; }
    bool is_quick_help_open() const noexcept { return show_quick_help_; }

    void open_options() noexcept { show_options_ = true; }
    void close_options() noexcept { show_options_ = false; }
    bool is_options_open() const noexcept { return show_options_; }

    void set_on_sfx_volume(std::function<void(float)> cb) { on_sfx_volume_ = std::move(cb); }
    void set_on_music_volume(std::function<void(float)> cb) { on_music_volume_ = std::move(cb); }
    void set_on_scroll_rate(std::function<void(float)> cb) { on_scroll_rate_ = std::move(cb); }
    void set_on_play_sfx(std::function<void(uint32_t)> cb) { on_play_sfx_ = std::move(cb); }
    void play_sfx(uint32_t sound_id) { if (on_play_sfx_) on_play_sfx_(sound_id); }

    float get_sfx_volume() const noexcept { return sfx_volume_; }
    float get_music_volume() const noexcept { return music_volume_; }
    float get_scroll_rate() const noexcept { return scroll_rate_; }
    bool is_chat_enabled() const noexcept { return chat_enabled_; }
    bool is_quick_help_enabled() const noexcept { return quick_help_enabled_; }

private:
    void render_top_bar(IRenderer& renderer, const assets::AssetArchive& assets, const sim::WorldState& world);
    void render_radar(IRenderer& renderer, const assets::AssetArchive& assets, const sim::WorldState& world, const ViewportCamera& camera);
    void render_selection_card(IRenderer& renderer, const assets::AssetArchive& assets, const sim::WorldState& world);
    void render_hatch_panel(IRenderer& renderer, const assets::AssetArchive& assets, const sim::WorldState& world);
    void render_action_buttons(IRenderer& renderer, const assets::AssetArchive& assets, const sim::WorldState& world);
    void render_news_banner(IRenderer& renderer, const assets::AssetArchive& assets, const sim::WorldState& world);
    void render_quit_dialog(IRenderer& renderer, const assets::AssetArchive& assets);
    void render_quick_help(IRenderer& renderer, const assets::AssetArchive& assets);
    void render_options_dialog(IRenderer& renderer, const assets::AssetArchive& assets);
    void render_marquee_box(IRenderer& renderer);

    void update_action_buttons_state(const sim::WorldState& world);
    void dispatch_targeted_order(int32_t world_x, int32_t world_y, sim::SimulationEngine& sim);
    void dispatch_move_order(int32_t target_tile_x, int32_t target_tile_y, sim::SimulationEngine& sim);
    void dispatch_attack_order(uint32_t target_enemy_id, sim::SimulationEngine& sim);
    void dispatch_smart_special_ability(int32_t world_x, int32_t world_y, sim::SimulationEngine& sim);

    uint8_t local_player_id_{0};
    uint32_t selected_ant_id_{0};
    std::vector<uint32_t> selected_ant_ids_{};
    int32_t selected_base_team_id_{-1};
    std::deque<std::string> chat_log_{};
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
    UIButton move_pedestal_button_{};
    UIButton ability_pedestal_button_{};
    UIButton stop_button_{};
    UIButton send_to_button_{};
    UIButton team_up_button_{};
    bool send_to_all_{true};

    // News Flash FIFO queue
    std::deque<NewsBannerItem> news_queue_{};
    uint32_t alarm_blink_ticks_{0};

    // Minimap drag navigation state
    bool is_radar_dragging_{false};

    // Top Header Buttons
    UIButton help_button_{};
    UIButton options_button_{};
    UIButton quit_button_{};

    // Dialog & Modal State
    bool show_quit_dialog_{false};
    UIButton yes_button_{};
    UIButton no_button_{};
    std::function<void()> on_quit_{nullptr};

    bool show_quick_help_{false};
    bool show_options_{false};

    // Options menu controls state
    float sfx_volume_{0.8f};
    float music_volume_{0.8f};
    float scroll_rate_{0.5f};
    bool chat_enabled_{true};
    bool quick_help_enabled_{false};
    bool opt_ok_button_pressed_{false};
    bool opt_return_button_pressed_{false};
    std::string quick_chat_keys_[4]{
        "Now you are in for it!$_",
        "Let me be!",
        "Attack!",
        "Do you want to ally?"
    };
    int active_slider_dragging_{-1}; // -1 none, 0 sfx, 1 music, 2 scroll
    uint32_t voice_variant_{0};
    std::function<void(float)> on_sfx_volume_{nullptr};
    std::function<void(float)> on_music_volume_{nullptr};
    std::function<void(float)> on_scroll_rate_{nullptr};
    std::function<void(uint32_t)> on_play_sfx_{nullptr};
};

} // namespace ants::app
