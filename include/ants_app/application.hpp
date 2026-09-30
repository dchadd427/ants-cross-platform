#pragma once

#include <cstdint>
#include <string>
#include <memory>
#include <array>

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
#include "ants_net/netgame.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_app/renderer.hpp"
#include "ants_app/hud.hpp"
#include "ants_app/scorecard.hpp"
#include "ants_app/screen_button.hpp"
#include "ants_app/audio_mixer.hpp"
#include "ants_app/config_store.hpp"
#include "ants_app/midi_player.hpp"
#include "ants_app/map_select.hpp"

namespace ants::app {

enum class AppState {
    Loading,
    QuickHelp,
    MapSelect,
    Playing
};

struct ApplicationConfig {
    std::string title{"Ants"};
    int window_width{1280};  // Default 2x integer scale
    int window_height{960};
    bool fullscreen{false};
    bool integer_scaling{false};
    bool headless{false};
    std::string chd_path{"Original-Ants/ants.chd"};
    std::string maps_dir{"Original-Ants/Maps"};     // the folder whose `*.lvl` files are the map list (the original searches its Maps folder)
    std::string default_map_path;                   // --map: the map of a game that starts without the setup screen; empty: the first map of the list
    std::string midi_path{"Original-Ants/INTRO.MID"};
    /// --settings: where the options are remembered (a file; the original keeps them in the registry). Empty: the per-user application folder (the browser's local
    /// storage in the web build), and nowhere in a headless run, which is what the tests use.
    std::string settings_path;
    uint32_t random_seed{1337};
    bool start_in_map_select{true};
    bool skip_intro{false};
    std::string screenshot_path{""};
    int screenshot_frames{5};
    int select_ant_id{-1};
    int select_base_team{-1};
    bool open_options{false};
    bool show_tile_grid{false};
    bool show_scorecard{false};
    uint8_t local_player_id{0};
    /// Network play: host a room, or join one (the room is the setup screen; see docs/NETWORK_PORT.md)
    enum class NetRole : uint8_t { None, Host, Join };
    NetRole net_role{NetRole::None};
    std::string net_address{"127.0.0.1"};       // Join: the host's address
    uint16_t net_port{4001};                    // the original's port
    bool net_loopback_only{false};              // Host: accept only this machine (two copies on one computer, tests)
    std::string player_name;                    // this player's name (--name); empty: the system user, or "Player" in a network game
    std::array<std::string, 4> team_names{};    // names of the teams of a local game (-N<team><name> as in the original, --team-name)
    /// false: a local game shows a score label only for the teams that have a name (and the local player's own). The browser build sets it: there are no
    /// other players in it (no multiplayer yet), so the colour words "Red:", "Blue:", "Black:" would only be placeholders. The original draws a label only for
    /// players that exist (docs 5.42).
    bool label_unnamed_teams{true};
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

    /// The command line: --map, --seed, --player / -pnum=, --name, -N<team><name> / --team-name, --host [port], --join host[:port], --port, --loopback,
    /// --headless, --fullscreen, --screenshot, ... (docs: README, Command-Line Options)
    static ApplicationConfig parse_arguments(int argc, char* argv[]);
    bool init(int argc, char* argv[]);
    bool init(const ApplicationConfig& config);
    int run();
    void run_frame();
    void run_frame_with_delta(float delta_time);
    void shutdown();

    bool is_running() const noexcept { return is_running_; }
    void quit();


    bool is_tile_grid_visible() const noexcept { return show_tile_grid_; }

    float get_current_fps() const noexcept { return current_fps_; }

    AppState state() const noexcept { return state_; }
    bool start_game(const std::string& map_path);
    void return_to_map_select();
    MapSelectScreen& map_select() noexcept { return map_select_; }
    uint8_t local_player_id() const noexcept { return local_player_id_; }
    void set_local_player(uint8_t team_id);

    ants::sim::SimulationEngine& sim() noexcept { return sim_; }
    Renderer& renderer() noexcept { return *renderer_; }
    HUD& hud() noexcept { return hud_; }
    ScorecardModal& scorecard() noexcept { return scorecard_; }
    MidiPlayer& midi_player() noexcept { return midi_player_; }
    AudioMixer& audio_mixer() noexcept { return audio_mixer_; }
    const ants::assets::AssetArchive& assets() const noexcept { return assets_; }

    /// The network of a room or a match (nullptr unless started with --host / --join)
    net::NetGame* net() noexcept { return net_.get(); }
    /// True while a room or a network match exists
    bool network_active() const noexcept { return net_ && net_->active(); }
    /// Advances the network by `dt` seconds of game time and handles what it reports (run once per frame; the tests call it directly)
    void pump_network(float dt);

    /// The quick help at the start (Ants.exe FUN_010145d2(1), keys FUN_010147c2): the START! button is the button class (captured at the press, acts at the release, leaving
    /// cancels); the keys Enter, Esc, C and X (either case) do the same; every other key and click does nothing (`M` would open the More Help dialog: not built).
    /// The loading screen ends: the quick help when the stored option asks for it, else the setup screen (public for the tests: a headless run has no loading screen)
    void finish_loading();
    void quick_help_move(int32_t x, int32_t y);
    void quick_help_press(int32_t x, int32_t y);
    void quick_help_release(int32_t x, int32_t y);
    void quick_help_key(SDL_Keycode sym);
    const ScreenButton& quick_help_start_button() const noexcept { return quick_help_start_; }

    void handle_key_down(const SDL_KeyboardEvent& key);
    void handle_mouse_motion(const SDL_MouseMotionEvent& motion);
    void handle_mouse_button(const SDL_MouseButtonEvent& button);
    void handle_camera_panning(float dt = 0.020f);
    void update_simulation(float dt);
    /// The music of the original is one sequencer device (docs 5.24e): the intro plays once, every piece that ends is followed by a random in-game piece, the match
    /// start, the activation of the program and the release of the music slider start one, the deactivation of the program and the end of a match close the device.
    /// `update_music` runs once per frame in every state (the tests call it directly).
    void update_music(float dt);
    /// WM_ACTIVATEAPP (0x100e875): the program loses / gets the input focus
    void set_app_active(bool active);
    /// The results screen (docs 5.49): a match that has ended opens it, in its waiting phase; its clock builds the rows 250 ms later and the machine's one winner or loser
    /// cue plays then. Once per frame in every state (the tests call it directly).
    void update_results(float dt);

    int32_t mouse_screen_x() const noexcept { return mouse_screen_x_; }
    int32_t mouse_screen_y() const noexcept { return mouse_screen_y_; }
    bool mouse_has_moved() const noexcept { return mouse_has_moved_; }
    void render_frame();

private:
    void handle_events();
    void play_ui_sound(uint32_t sound_id);
    void release_ui_sounds();
    static constexpr uint32_t kUiPressOwner = 0x80000001u;      // the sound owner of the button that is being pressed (there is only one at a time)

    ApplicationConfig config_{};
    AppState state_{AppState::MapSelect};
    bool is_running_{false};
    bool is_paused_{false};
    bool show_tile_grid_{false};

    SDL_Window* window_{nullptr};
    std::unique_ptr<Renderer> renderer_;

    ants::assets::AssetArchive assets_;
    ants::assets::LevelData current_level_;
    ants::sim::SimulationEngine sim_;

    MapSelectScreen map_select_;
    ConfigStore config_store_;      // the options that the program remembers; the HUD writes to it (before hud_: the HUD is destroyed first)
    HUD hud_;
    ScorecardModal scorecard_;
    AudioMixer audio_mixer_;
    MidiPlayer midi_player_;

    uint8_t local_player_id_{0};
    std::unique_ptr<net::NetGame> net_;
    double net_time_ms_{0.0};
    bool match_over_handled_{false};
    std::string net_notice_;
    std::string player_name_;
    int32_t mouse_screen_x_{320};
    int32_t mouse_screen_y_{240};
    bool mouse_has_moved_{false};

    // The match set-up shared by the local game and the network game
    bool load_match(const std::string& map_path, uint32_t seed, uint8_t roster, bool fog);   // level, simulation, renderer (no HUD, no sound)
    void enter_match();                                   // music, start sound, camera, HUD reset, "Get ready", state Playing
    void post_tick();                                     // what every simulation tick shows: HUD, events, audio, the end of the match
    void check_match_over();                              // the match is over and not yet shown: the results screen opens (waiting), the music closes
    void confirm_quit();                                  // the quit dialog's Yes (FUN_0101453f): the quit ends the match while one other side is left, else the player leaves

    // Network play
    void handle_net_events();
    void net_load_match();                                // the host said Start: load the map, initialise the simulation, report
    void net_begin_match();                               // everybody loaded: the match runs on this machine
    void net_end_session(const std::string& notice);      // leave the room / the match and return to the local setup screen
    void net_start_from_setup(const std::string& map_path);
    void sync_room_view();
    void render_net_overlay();
    void apply_team_names(const std::array<std::string, 4>& names, uint8_t roster);   // simulation texts, HUD labels, results rows

    void show_start_view();                               // the view at the start of a match: scrolled just far enough to show the square around the hill's anchor tile
    void enter_map_select();                              // the setup screen is created (again): its labels stay empty until its refresh

    void play_next_ingame_music();
    void start_intro_music();
    void close_music();
    void play_startup_sound();
    void render_loading_screen();
    void render_quick_help_screen();

    // Intro & Loading state
    uint32_t intro_ticks_{0};
    ScreenButton quick_help_start_{528, 437, 99, 27};       // START!: the union of qh_start1 / 2 (529, 437, 98 x 27) and qh_start3 (528, 438, 97 x 24)

    // 20 Hz Discrete Simulation Timing
    uint64_t last_frame_time_{0};
    int headless_frame_count_{0};
    float tick_accumulator_{0.0f};
    float input_accumulator_{0.0f};     // the 50 ms input task (edge scrolling, minimap drag)
    float current_fps_{60.0f};
    int last_music_track_{-1};
    bool music_open_{false};            // the sequencer device is open (the intro or a piece was started and not closed): its end starts the next random piece
    bool music_resume_on_activate_{false};   // the device was open when the program was deactivated (0x5588)

    // Smoothed FPS Display and Frametime Sparkline
    static constexpr size_t SPARKLINE_SAMPLES = 36;
    std::array<float, SPARKLINE_SAMPLES> frametime_history_{};
    size_t frametime_index_{0};
    float fps_display_value_{60.0f};
    float fps_time_accumulator_{0.0f};
    int fps_frame_counter_{0};
};

} // namespace ants::app
