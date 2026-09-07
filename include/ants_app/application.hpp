#pragma once

#include <cstdint>
#include <string>
#include <memory>
#include <vector>

#if defined(__has_include)
  #if __has_include(<SDL.h>)
    #include <SDL.h>
  #elif __has_include(<SDL2/SDL.h>)
    #include <SDL2/SDL.h>
  #endif
#else
  #include <SDL2/SDL.h>
#endif

#include "ants_assets/asset_archive.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_app/renderer.hpp"
#include "ants_app/hud.hpp"
#include "ants_app/scorecard.hpp"
#include "ants_app/audio_mixer.hpp"
#include "ants_app/midi_player.hpp"
#include "ants_app/map_select.hpp"

namespace ants::app {

enum class AppState {
    MapSelect,
    Playing
};

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
    std::string midi_path{"Original-Ants/INTRO.MID"};
    uint32_t random_seed{1337};
    bool start_in_map_select{true};
    std::string screenshot_path{""};
    int screenshot_frames{5};
    int select_ant_id{-1};
    int select_base_team{-1};
    bool open_options{false};
    bool show_tile_grid{false};
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

    bool is_unit_health_visible() const noexcept { return show_unit_health_; }
    void set_unit_health_visible(bool visible) noexcept { show_unit_health_ = visible; }
    void toggle_unit_health_visibility() noexcept { show_unit_health_ = !show_unit_health_; }

    bool is_tile_grid_visible() const noexcept { return show_tile_grid_; }
    void set_tile_grid_visible(bool visible) noexcept { show_tile_grid_ = visible; }
    void toggle_tile_grid_visibility() noexcept { show_tile_grid_ = !show_tile_grid_; }

    AppState state() const noexcept { return state_; }
    void set_state(AppState st) noexcept { state_ = st; }
    bool start_game(const std::string& map_path);
    void return_to_map_select();
    MapSelectScreen& map_select() noexcept { return map_select_; }

    const ApplicationConfig& config() const noexcept { return config_; }
    ants::sim::SimulationEngine& sim() noexcept { return sim_; }
    Renderer& renderer() noexcept { return *renderer_; }
    HUD& hud() noexcept { return hud_; }
    ScorecardModal& scorecard() noexcept { return scorecard_; }
    AudioMixer& audio_mixer() noexcept { return audio_mixer_; }
    MidiPlayer& midi_player() noexcept { return midi_player_; }
    const ants::assets::AssetArchive& assets() const noexcept { return assets_; }

private:
    void handle_events();
    void handle_key_down(const SDL_KeyboardEvent& key);
    void handle_mouse_motion(const SDL_MouseMotionEvent& motion);
    void handle_mouse_button(const SDL_MouseButtonEvent& button);

    void update_simulation(float dt);
    void render_frame();

    ApplicationConfig config_{};
    AppState state_{AppState::MapSelect};
    bool is_running_{false};
    bool is_paused_{false};
    bool show_unit_health_{false};
    bool show_tile_grid_{false};

    SDL_Window* window_{nullptr};
    std::unique_ptr<Renderer> renderer_;

    ants::assets::AssetArchive assets_;
    ants::assets::LevelData current_level_;
    ants::sim::SimulationEngine sim_;

    MapSelectScreen map_select_;
    HUD hud_;
    ScorecardModal scorecard_;
    AudioMixer audio_mixer_;
    MidiPlayer midi_player_;

    uint8_t local_player_id_{0};
    int32_t mouse_screen_x_{0};
    int32_t mouse_screen_y_{0};

    // 20 Hz Discrete Simulation Timing
    uint64_t last_tick_time_{0};
    float tick_accumulator_{0.0f};
};

} // namespace ants::app
