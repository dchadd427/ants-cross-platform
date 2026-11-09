#include "ants_app/application.hpp"
#include "ants_app/net_overlay.hpp"
#include "ants_app/version.hpp"
#include "ants_ai/bot.hpp"
#include <iostream>
#include <algorithm>
#include <cstdlib>
#if defined(__EMSCRIPTEN__)
  #include <emscripten.h>
  #include <emscripten/html5.h>                                  // the page's visibility (a hidden page runs no frames, see Application::set_page_hidden)
#endif
#include "application_internal.hpp"

#if defined(__EMSCRIPTEN__)
// Tells the page that a game on this computer began (web/shell.html antsReportLocalGame: the page asks the server to count it, and ignores every failure); nothing here can throw
extern "C" {
EM_JS(void, ants_report_local_game, (), {
    try { if (typeof antsReportLocalGame === "function") antsReportLocalGame(); } catch (e) {}
});
}

// The match of a server's room is over: the page is told what the results show (Application::match_over_json), before the game lets go of the key of its seat (web/front/rematch.js reads it then)
extern "C" {
EM_JS(void, ants_match_over, (const char* json), {
    try { if (typeof antsMatchOver === "function") antsMatchOver(UTF8ToString(json)); } catch (e) {}
});
}
EM_JS_DEPS(match_over, "$UTF8ToString");

// The one application of the page (main() keeps it alive for as long as the page lives): the entry for ants_background_pump below
ants::app::Application* g_web_app = nullptr;       // (also read by the exports in application_touch.cpp)
#endif

namespace ants::app {

using namespace application_detail;

namespace {

// 0x100e714: the Music Volume v (0 .. 100) becomes the device's volume word v * 0xffff / 100 (integer division); as a fraction of the full scale
float music_volume_of(int32_t v) {
    const int32_t word = (std::clamp(v, 0, 100) * 0xffff) / 100;
    return static_cast<float>(word) / 65535.0f;
}

// A frame or a background step holds the flag while it runs: nothing may start another one inside it (Application::run_frame_with_delta, background_pump_after)
struct AdvanceGuard {
    explicit AdvanceGuard(bool& flag) : flag_(flag) { flag_ = true; }
    ~AdvanceGuard() { flag_ = false; }
    AdvanceGuard(const AdvanceGuard&) = delete;
    AdvanceGuard& operator=(const AdvanceGuard&) = delete;

private:
    bool& flag_;
};

#if defined(__EMSCRIPTEN__)
// The browser's visibilitychange (registered in init): the page was hidden (a background tab, a minimised window) or is shown again
EM_BOOL on_visibility_change(int, const EmscriptenVisibilityChangeEvent* e, void* user) {
    static_cast<Application*>(user)->set_page_hidden(e->hidden != 0);
    return EM_FALSE;
}
#endif

} // anonymous namespace

Application::Application() = default;

Application::~Application() {
    shutdown();
}

bool Application::init(int argc, char* argv[]) {
    return init(parse_arguments(argc, argv));
}

bool Application::init(const ApplicationConfig& config) {
    config_ = config;
    show_tile_grid_ = config_.show_tile_grid;

    // A command line that is wrong, or bots that cannot play here (a seat that is taken, --join: a guest never runs bots), refuse to start: before anything is opened
    if (!config_.startup_error.empty()) {
        std::cerr << "[Application] " << config_.startup_error << std::endl;
        return false;
    }
    const bool hosting = config_.net_role == ApplicationConfig::NetRole::Host;
    const uint8_t own_seat = hosting ? uint8_t{0} : (config_.local_player_id < 4 ? config_.local_player_id : uint8_t{0});      // (a room's host sits at seat 0)
    if (const std::string why = bot_setup_problem(own_seat, false); !why.empty()) {
        std::cerr << "[Application] " << why << std::endl;
        return false;
    }
    const bool local_bots = !config_.bots.empty() && config_.net_role == ApplicationConfig::NetRole::None;
    const bool local_seats = local_bots || game_for_one();                       // the seats were chosen: you and the bots, or you alone (--alone); every other seat has no colony
    local_roster_ = local_seats ? bot_roster(own_seat) : uint8_t{0x0F};

    // 1. Initialize SDL2
    uint32_t sdl_flags = SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER;
    if (config_.headless) {
        SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
    }

    // Several games on one screen: the click that activates a window is also a click in it (the first click on a background window is not lost)
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");

    // A touch screen's fingers are the touch model's (application_touch.cpp): SDL's own emulation, which made the first finger a left mouse button, is off so that nothing reaches the game twice. At
    // SDL's highest priority: an environment variable SDL_TOUCH_MOUSE_EVENTS=1 would beat an ordinary SDL_SetHint, and the fingers would reach the game twice.
    SDL_SetHintWithPriority(SDL_HINT_TOUCH_MOUSE_EVENTS, "0", SDL_HINT_OVERRIDE);

    if (SDL_Init(sdl_flags) != 0) {
        std::cerr << "[Application] SDL_Init Error: " << SDL_GetError() << std::endl;
        return false;
    }

    // 2. Load Master Asset Archive (ants.chd)
    if (!assets_.load_from_file(config_.chd_path)) {
        std::cerr << "[Application] Failed to load CHD archive: " << config_.chd_path << std::endl;
        return false;
    }

    // A replay (--replay): the file is read and checked here, before any screen; whatever is wrong with it is the replay's failure, the game starts either way (the page tells the player)
    if (!config_.replay_path.empty()) prepare_replay();

    // 3. Load Map Level: the list is what the Maps folder holds; a game that starts without the setup screen plays --map, else the map that the setup screen highlights (TREASURE.LVL when the folder
    // holds it, else the first map of the list: MapSelectScreen::init)
    map_select_.init(config_.maps_dir);
    if (!map_fetcher_) map_fetcher_ = make_web_map_fetcher();            // (the browser: the players' maps come from the site, application_maps.cpp)
    if (config_.default_map_path.empty()) config_.default_map_path = map_select_.get_selected_map_path();
    if (config_.default_map_path.empty()) {
        std::cerr << "[Application] There is no map (*.lvl) in " << config_.maps_dir << std::endl;
        return false;
    }
    ants::assets::LevelValidation start_verdict;
    if (!current_level_.load_from_file(config_.default_map_path, &start_verdict, ants::assets::LevelLimits::for_play_extended())) {
        std::cerr << "[Application] Failed to load Level: " << config_.default_map_path << std::endl;
        if (start_verdict.first_fatal()) std::cerr << "[Application]   " << start_verdict.reason() << std::endl;
        return false;
    }
    // A game that starts straight into its match (--map, no setup screen, no network) plays the seats that are taken (all four, or you and the bots, or you alone): the level must be
    // playable by them, as load_match demands of every match that starts from the setup screen (a start marker outside the grid has no behaviour in the original)
    if (!config_.start_in_map_select && config_.net_role == ApplicationConfig::NetRole::None) {
        start_verdict = current_level_.validate(local_roster_, ants::assets::LevelLimits::for_play_extended());
        if (!start_verdict.playable) {
            std::cerr << "[Application] The level cannot be played: " << config_.default_map_path << std::endl;
            std::cerr << "[Application]   " << start_verdict.reason() << std::endl;
            return false;
        }
    }

    // 4. Initialize Simulation Engine. A local game that starts at once with bots (or --alone) plays the seats that are taken (you and the bots): a team nobody plays has no hill
    // and no ants (the setup screen's game does the same in start_game)
    sim_.set_game_mode(static_cast<sim::GameMode>(config_.game_mode));     // (a game of this machine plays by --game-mode; every init() after this one keeps it, "Play again" too. A room's match sets its own in load_match)
    if (local_seats && !config_.start_in_map_select) {
        if (local_roster_ != 0x0F) current_level_ = current_level_.for_roster(local_roster_);
        sim_.init(current_level_, config_.random_seed, local_roster_);
    } else {
        sim_.init(current_level_, config_.random_seed);
    }

    // 5. Initialize Audio Subsystem
    audio_mixer_.set_headless_mode(config_.headless);
    audio_mixer_.init(assets_);
    if (!config_.headless) {
        audio_mixer_.init_sdl_audio(1024);
    }

    midi_player_.set_headless_mode(config_.headless);
    midi_player_.load_file(config_.midi_path);

    // The settings that the program remembers (the original reads its profile in the world's constructor, FUN_0100a2c9): loaded before anything is applied, the aspect among them;
    // a headless run keeps them in memory only unless --settings names a file
    config_store_.set_location(!config_.settings_path.empty() ? config_.settings_path : (config_.headless ? std::string() : ConfigStore::default_location()));
    config_store_.load();
    choose_aspect();
    choose_zoom();
    choose_prediction();
    make_rejoin_store();

    // 6. Create Desktop Window
    uint32_t win_flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    if (config_.fullscreen) win_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    if (config_.headless)   win_flags = SDL_WINDOW_HIDDEN;

    window_created_ = initial_window_rect();                 // (the shape of the picture that it will show: no frame is ever shown in another shape)
    window_ = SDL_CreateWindow(
        config_.title.c_str(),
        window_created_.x, window_created_.y,
        window_created_.w, window_created_.h,
        win_flags
    );
    if (!window_) {
        std::cerr << "[Application] Failed to create SDL Window: " << SDL_GetError() << std::endl;
        return false;
    }

    window_title_ = config_.title;
    apply_window_layout();
    if (!config_.headless) {
        SDL_RaiseWindow(window_);
        update_mouse_grab();
        // the game draws its own cursor: the system cursor is hidden while the pointer is over the window and shown everywhere else (other windows,
        // the desktop: nothing grabs or hides the pointer outside the game); SDL tells when it enters and leaves (handle_window_event)
        SDL_PumpEvents();
        pointer_outside_ = SDL_GetMouseFocus() != window_;
        SDL_ShowCursor(pointer_outside_ ? SDL_ENABLE : SDL_DISABLE);
    }

    // 7. Initialize Renderer
    renderer_ = std::make_unique<Renderer>();
    if (!renderer_->init(window_, assets_)) {
        std::cerr << "[Application] Failed to initialize Renderer" << std::endl;
        return false;
    }

    renderer_->set_canvas_size(canvas_width_of(aspect_), canvas_height_of(aspect_));         // SDL's logical size: the picture that the window shows
    layout_ = ScreenLayout::with_size(canvas_width_of(aspect_), canvas_height_of(aspect_)); // the match screen is as big as the canvas (the original's own screen for 4:3, the wide frame for 16:9)
    picture_ = picture_for_state();                                                          // every screen is the whole canvas (a match of a smaller layout is centred in it)
    renderer_->set_picture(picture_);
    renderer_->set_layout(layout_);
    renderer_->set_level(current_level_);

    apply_match_zoom();
    show_start_view();

    // 8. Initialize HUD and Scorecard
    hud_.set_text_metrics(renderer_.get());
    hud_.set_layout(layout_);
    hud_.set_game_mode(sim_.game_mode());
    hud_.init(0);
    hud_.options().load(config_store_);
    hud_.set_config_store(&config_store_);
    load_game_menu_settings();                                        // (Master volume, Mute and the hit point numbers: application_game_menu.cpp)
    local_player_id_ = 0;

    scorecard_.set_on_replay([this]() {
        if (network_active()) {                                       // a network match is not replayed here: back to the setup screen
            return_to_map_select();
            return;
        }
        scorecard_.hide();
        update_picture();                                             // the match screen is back
        if (bots_) {                                                  // the same seats play again, with new bots
            stop_bots();
            sim_.init(current_level_, config_.random_seed + 1, local_roster_);
            begin_local_recording(config_.random_seed + 1, false);   // (no teams again: init() leaves none)
            start_local_bots(config_.random_seed + 1);
        } else {
            sim_.init(current_level_, config_.random_seed + 1);
            begin_local_recording(config_.random_seed + 1, false);
        }
        match_over_handled_ = false;
        hud_.reset();
        midi_player_.stop(); // In-game music stays silent
    });

    scorecard_.set_on_quit([this]() {
        leave_game();                                         // (a network match that the start menu led to: back to the menu; a local match, and every run without a menu: the program ends)
    });
    scorecard_.set_on_play_sfx([this](uint32_t sound_id) { play_ui_sound(sound_id); });

    hud_.set_sim_query(&sim_);           // the cursor asks the simulation whether a tile is a valid special target
    hud_.set_on_quit([this]() {
        confirm_quit();
    });
    hud_.set_on_menu_request([this]() { request_game_menu(); });         // (Esc with nothing to cancel: the page's settings panel)
    hud_.set_on_fullscreen([this]() { toggle_fullscreen(); });          // (the countdown card's Fullscreen button, protocol 21: the native game's own; the page's button does it in the browser)

    hud_.set_on_sfx_volume([this](int32_t v) {
        audio_mixer_.set_sound_volume(v);                       // 0x102d7cf: the Sound Volume 0 .. 100 enters every sound's law
    });

    hud_.set_on_music_volume([this](int32_t v) {
        const float volume = music_volume_of(v);
        audio_mixer_.set_music_volume(volume);
        midi_player_.set_volume(volume);
        if (music_open_) play_next_ingame_music();              // 0x100e714: the option is applied at the release; a running piece is closed and a new random one starts
    });

    hud_.set_on_play_sfx([this](uint32_t sound_id) { play_ui_sound(sound_id); });
    hud_.set_news_note([this](const sim::NewsEvent& event) { return declined_team_up_note(event); });     // (a computer player that declines to team up says why)

    hud_.set_on_spawn_click_marker([this](int32_t wx, int32_t wy) {
        if (renderer_) {
            renderer_->spawn_transient_effect("xmarks", wx, wy);
        }
    });

    audio_mixer_.set_sound_volume(hud_.get_sound_volume());
    audio_mixer_.set_music_volume(music_volume_of(hud_.get_music_volume()));
    midi_player_.set_volume(music_volume_of(hud_.get_music_volume()));

    // 9. Initialize Map Selection Screen
    const bool networked = config_.net_role != ApplicationConfig::NetRole::None;
    const size_t my_team = config_.local_player_id < 4 ? config_.local_player_id : 0u;
    // The name of this player: --name, else the -N name of its team, else the system user (a network game never sends the user and machine name
    // by default: it says "Player" unless --name is given)
    std::string player_name = !config_.player_name.empty() ? config_.player_name
                              : (!config_.team_names[my_team].empty() ? config_.team_names[my_team]
                                                                       : (networked ? std::string("Player") : get_system_username()));
    player_name_ = player_name;
    local_player_name_ = player_name;
    map_select_.set_player_name(player_name);
    map_select_.set_player_team(config_.local_player_id);
    scorecard_.set_local_player_name(player_name);
    hud_.set_player_name(player_name);
    if (config_.local_player_id < 4) {
        set_local_player(config_.local_player_id);
    }
    SDL_StartTextInput();
    map_select_.set_on_start([this](const std::string& map_path) {
        if (network_active()) net_start_from_setup(map_path);
        else start_game(map_path);
    });
    map_select_.set_on_map_changed([this](const std::string& filename) {
        if (net_ && net_->is_host()) net_->set_map(filename);
    });
    map_select_.set_on_fog_changed([this](bool fog) {
        if (net_ && net_->is_host()) {
            net_->set_fog(fog);
            if (fog && !net_->room().fog) map_select_.follow_host_choice(net_->room().map_name, false);      // refused (a bot would see through it): the screen shows Off again
        }
    });
    map_select_.set_on_request_start([this]() { net_request_start(); });
    map_select_.set_on_move_seat([this](uint8_t seat) { net_move_seat(seat); });
    map_select_.set_on_quit([this]() {
        leave_game();                                         // (a network game that the start menu led to: back to the menu)
    });
    map_select_.set_on_play_sfx([this](uint32_t sound_id) { play_ui_sound(sound_id); });

    // A network game: the room is the setup screen (host: pick the map and START; guest: follow the host's choice)
    if (networked) {
        config_.start_in_map_select = true;
        net_ = std::make_unique<net::NetGame>(sim_);
        hook_rejoin_store();
        net_->set_discovery(config_.lan_port);                                          // an open room announces itself to the local network (ants_net/lan.hpp)
        net_->set_game_version(std::string(VERSION_STRING));
        if (config_.net_platform != 0) net_->set_platform(config_.net_platform);        // (--platform, or the word of the page's address; else the build's own)
        if (config_.net_create) net_->set_create(*config_.net_create);                  // (the room that a server makes when it has none of the code: --room-map, --room-seats, ...)
        const bool host_role = config_.net_role == ApplicationConfig::NetRole::Host;
        // A join that names a room of a server joins with the key of the seat that this machine had there, when the store has a fresh one (a game that was closed, a page that was reloaded): the
        // newest entry of that server and room, or of the seat that --seat asks for. The key goes into the Hello only; the seat is the entry's, so that the key can be let go of by its seat.
        net::SeatKey join_key{};
        uint8_t join_seat = config_.net_seat;
        if (!host_role && !config_.net_room.empty() && rejoin_store_) {
            const std::string server = rejoin_server_text(config_.net_address, config_.net_port, config_.net_url);
            if (const std::optional<RejoinEntry> kept = rejoin_store_->find(server, config_.net_room, config_.net_seat)) {
                join_key = kept->key;
                join_seat = kept->seat;
            }
        }
        const bool ok = host_role                   ? net_->host(config_.net_port, player_name, config_.net_loopback_only)
                        : !config_.net_url.empty() ? net_->join_url(config_.net_url, player_name, join_seat, config_.net_room, config_.net_token, join_key)
                                                   : net_->join(config_.net_address, config_.net_port, player_name, join_seat, config_.net_room, config_.net_token, join_key);
        if (!ok && !host_role && !config_.net_url.empty()) {
#if defined(__EMSCRIPTEN__)
            std::cerr << "[Application] Could not join " << config_.net_url << " (not a usable ws:// or wss:// address, or this browser has no WebSocket)" << std::endl;
#else
            std::cerr << "[Application] Could not join " << config_.net_url << ": --join-url works in the web build only, a native game joins with --join host[:port]" << std::endl;
#endif
            return false;
        }
        if (!ok) {
            std::cerr << "[Application] Could not " << (host_role ? "open a room on port " : "reach the host at ")
                      << (host_role ? std::to_string(config_.net_port)
                                    : !config_.net_url.empty() ? config_.net_url : config_.net_address + ":" + std::to_string(config_.net_port))
                      << std::endl;
            return false;
        }
        for (const ai::BotSpec& bot : config_.bots) {                               // the computer players of a room: seated before any guest comes
            if (net_->is_host() && !net_->add_bot(bot.seat, ai::bot_display_name(bot))) {
                std::cerr << "[Application] Could not seat the bot at seat " << static_cast<unsigned>(bot.seat) << std::endl;
                return false;
            }
        }
        attach_net();
        if (net_->is_host() && config_.game_mode != 0) net_->set_mode(config_.game_mode);       // (--game-mode: the room's Start carries it to every machine)
        if (net_->is_host() && !map_select_.get_maps().empty()) net_->set_map(map_select_.get_maps()[static_cast<size_t>(map_select_.get_selected_index())].filename);
        sync_room_view();
    } else {
        // the names of a local game (the local player's own name too); a bot is called "Bot (Medium)" unless -N / --team-name says otherwise
        apply_team_names(local_bots ? local_team_names() : config_.team_names, local_seats && !config_.start_in_map_select ? local_roster_ : uint8_t{0x0F});
    }

    // The desktop start menu: part of this run when the config asks for it and nothing starts a match or a room at once (never in the web build)
#if !defined(__EMSCRIPTEN__)
    menu_enabled_ = config_.start_menu && config_.start_in_map_select && !networked;
#endif
    if (menu_enabled_) init_start_menu();
    play_pending_ = config_.play_at_once && !networked && !menu_enabled_;      // --play: a game of this machine, without the menu, starts its map at the first visit of the setup screen

    // --audio-focus: a window that opened behind the others never receives "focus lost": it starts silent and holds its music until it gets the focus
    if (config_.audio_follows_focus && !config_.headless && window_ != nullptr) {
        SDL_PumpEvents();
        if ((SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS) == 0) set_app_active(false);
    }

    // Determine initial AppState & audio lifecycle
    if (replay_mode_) {
        audio_mixer_.stop_music();
        midi_player_.stop();
        config_.label_unnamed_teams = true;                          // (every seat of the match is shown, by its name or its color)
        start_replay();
    } else if (!config_.start_in_map_select) {
        state_ = AppState::Playing;
        match_started_ = true;
        audio_mixer_.stop_music();
        midi_player_.stop();
        if (config_.select_ant_id > 0) {
            hud_.select_ant(static_cast<uint32_t>(config_.select_ant_id));
            if (renderer_) {
                for (const auto& a : sim_.get_world_state().ants) {
                    if (a.id == static_cast<uint32_t>(config_.select_ant_id)) {
                        renderer_->camera().center_on(a.px, a.py, current_level_.width(), current_level_.height());
                        break;
                    }
                }
            }
        } else if (config_.select_base_team >= 0) {
            hud_.select_base(config_.select_base_team);
        }
        if (config_.open_options) {
            hud_.open_options();
        }
        if (config_.show_scorecard) {
            sim::MatchResult mr{};                                   // a sample: four teams, the first two allied
            mr.is_over = true;
            mr.ally = {1, 0, sim::ALLIANCE_NONE, sim::ALLIANCE_NONE};
            const int32_t scores[4] = {900, 600, 420, 100};
            const uint32_t lost[4] = {5, 12, 20, 25};
            const uint32_t killed[4] = {18, 8, 4, 1};
            const uint32_t hatched[4] = {25, 15, 10, 5};
            for (size_t p = 0; p < 4; ++p) {
                mr.stats[p].score = scores[p];
                mr.stats[p].friendly_lost = lost[p];
                mr.stats[p].enemy_killed = killed[p];
                mr.stats[p].new_hatched = hatched[p];
            }
            mr.decide_winners();
            scorecard_.show(mr, 0);
            scorecard_.update(0.25f);                                // the preview shows the rows, not the waiting label
        }
        const bool teams_made = form_start_teams();                  // --teams: the teams are made before the first tick
        if (!networked) {
            use_local_sink();
            begin_local_recording(config_.random_seed, teams_made);
        }
        if (local_seats) {                                           // --map with --bot or --alone: the game is running already, the bots (if any) join it
            hud_.set_roster_mask(local_roster_);                     // (the bots are named: every taken seat has its label and its row)
            scorecard_.set_shown_teams(local_roster_);
            start_local_bots(config_.random_seed);                   // (nothing without bots)
        }
    } else {
        if (!config_.skip_intro && !config_.headless) {
            state_ = AppState::Loading;
            intro_ticks_ = 0;
        } else if (menu_enabled_) {
            enter_start_menu();
        } else {
            enter_map_select();
        }
        start_intro_music();             // plays once, then the random in-game pieces follow (also on the setup screen)
    }

    update_picture();                                        // (a game that starts in its match: the match screen is the whole canvas)
    mouse_screen_x_ = picture_.w / 2;                        // (the pointer starts in the middle of the screen that is up)
    mouse_screen_y_ = picture_.h / 2;
    is_running_ = true;
    frametime_history_.fill(16.666f);
    frametime_index_ = 0;
    fps_display_value_ = 60.0f;
    fps_time_accumulator_ = 0.0f;
    fps_frame_counter_ = 0;
    mouse_has_moved_ = false;
#if defined(__EMSCRIPTEN__)
    // The browser tells when the page is hidden or shown (visibilitychange); a page may also have been opened in a background tab, so ask once now
    g_web_app = this;
    emscripten_set_visibilitychange_callback(this, EM_FALSE, on_visibility_change);
    refresh_page_visibility();
    set_on_local_match_started([]() { ants_report_local_game(); });       // the page counts the games on this computer for the front page (a native game has no such hook)
    on_seat_known_ = [](uint8_t seat) {                          // the page puts the seat into its address (web/shell.html: antsSeatKnown)
        const std::string script = "try{if(window.antsSeatKnown)window.antsSeatKnown(" + std::to_string(static_cast<unsigned>(seat)) + ");}catch(e){}";
        emscripten_run_script(script.c_str());
    };
    on_match_over_ = [](const std::string& json) { ants_match_over(json.c_str()); };      // the page's rematch strip (web/front/rematch.js)
#endif
    return true;
}

void Application::shutdown() {
    is_running_ = false;
    finish_recording();                                   // (a match that the program ends in the middle of: its file, as for any match that is left)
    if (menu_enabled_) start_menu_.flush();
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
    if (presentation_hidden_) {                           // (closed inside a fullscreen Space that the game made the Dock and the menu bar leave: the system's own behaviour comes back)
        presentation_hidden_ = false;
        set_presentation_options(0ul);
    }
#endif
#if !defined(__EMSCRIPTEN__)
    if (!config_.headless && match_started_) {
        const std::string folder = ConfigStore::default_folder();
        if (!folder.empty()) write_chat_transcript(folder + "chat.txt");      // FUN_01010210: "Exiting...", then the chat log goes to chat.txt
    }
#endif
    if (net_) net_->leave();                              // the others see a clean goodbye, not a dead connection

    midi_player_.shutdown();
    audio_mixer_.shutdown_sdl_audio();

    if (renderer_) {
        hud_.set_text_metrics(nullptr);                       // the HUD measures its chat with the renderer that goes away here
        renderer_->shutdown();
        renderer_.reset();
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    SDL_Quit();
}

void Application::run_frame_with_delta(float delta_time) {
    if (!is_running_ || advancing_) return;                  // (a frame never starts inside a frame or inside a background step)
    const AdvanceGuard advance(advancing_);
    last_frame_run_ = now_counter();                         // (the wake-ups of a hidden page stand down while frames come)
    apply_pending_music();                                   // a frame is what a page that is drawn runs: the music that a background step left for it starts now

    // The network's clock keeps real time (a frame counts for at most a second of it), unlike the local simulation's below: the lock-step runner has its own bounds (it pays
    // back at most 400 ms and runs the rest down at double speed). With the clamp a hitch of 400 ms left three turns standing in its queue for the rest of the match.
    const float net_delta_time = std::min(delta_time, 1.0f);
    // The frame rate counter is the real rate down to one frame a second: a frame counts for at most a second here as it does for the network's clock (with the local
    // simulation's clamp of 100 ms below, every rate under 10 frames a second read "10 FPS")
    const float fps_delta_time = net_delta_time;

    // Clamp delta_time to prevent physics / tick spiral when tab or window is backgrounded
    if (delta_time > 0.100f) {
        delta_time = 0.100f;
    }

    if (fps_delta_time > 0.0001f) {
        float instant_fps = 1.0f / fps_delta_time;
        current_fps_ = current_fps_ * 0.9f + instant_fps * 0.1f;

        // Record frametime in ms for sparkline (the bars end at 33 ms: a longer frame is a full bar either way)
        float frame_ms = delta_time * 1000.0f;
        frametime_history_[frametime_index_] = frame_ms;
        frametime_index_ = (frametime_index_ + 1) % SPARKLINE_SAMPLES;

        // Rolling average: update displayed FPS every 250ms (~4 times/sec) for clear, stable readability
        fps_time_accumulator_ += fps_delta_time;
        fps_frame_counter_++;
        if (fps_time_accumulator_ >= 0.25f) {
            fps_display_value_ = static_cast<float>(fps_frame_counter_) / fps_time_accumulator_;
            fps_time_accumulator_ = 0.0f;
            fps_frame_counter_ = 0;
        }
    }

    handle_events();

    enforce_zoom_limits();                      // the camera's zoom is one that is allowed and drawn (never below the map's limit; no offscreen target: 1)

    handle_camera_panning(delta_time);

    pump_network(net_delta_time);               // a network match has no pause: it keeps running while the window is in the background

    if (state_ == AppState::MapSelect) map_select_.update(delta_time);
    update_results(delta_time);
    update_music(delta_time);                   // the music chain runs in every state (the intro ends on the loading, quick help or setup screen alike)

    if (!is_paused_) {
        update_simulation(delta_time);
    }

    if (!config_.screenshot_path.empty()) {
        if (--config_.screenshot_frames == 1) {
            renderer_->request_screenshot(config_.screenshot_path);
        } else if (config_.screenshot_frames <= 0) {
            is_running_ = false;
        }
    }

    render_frame();

    if (config_.headless && config_.screenshot_path.empty()) {
        if (++headless_frame_count_ >= 10) {
            is_running_ = false;
        }
    }
}

void Application::run_frame() {
    uint64_t perf_freq = SDL_GetPerformanceFrequency();
    uint64_t current_time = now_counter();
    float delta_time = (last_frame_time_ > 0) ? (static_cast<float>(current_time - last_frame_time_) / static_cast<float>(perf_freq)) : 0.01666f;
    last_frame_time_ = current_time;

    run_frame_with_delta(delta_time);
}

// ------------------------------------------------------------------------------------------------
// A hidden page (see the comment at set_page_hidden in application.hpp)
// ------------------------------------------------------------------------------------------------

void Application::set_page_hidden(bool hidden) {
    if (hidden == page_hidden_) return;
    if (hidden) {
        cancel_touch();                                      // (the browser may never say that a finger lifted while the page was away)
        cancel_touch_queued();                               // (and the finger events that SDL holds are followed by the cancel too)
        page_hidden_ = true;                                 // from now on the wake-ups may drive a network match: the first one counts the time since the last frame
        last_frame_run_ = 0;                                 // (only a frame that runs from now on shows that the browser still delivers frames)
        hidden_since_ = now_counter();
        hidden_wakes_ = 0;
        hidden_ticks_ = 0;
        return;
    }
    if (background_driven()) background_step();              // shown again: one last wake-up so that the clocks are up to date, then the frame loop has the match back
    page_hidden_ = false;
    last_frame_time_ = now_counter();                        // the first frame measures from here: the hours that the page was hidden are not a frame
    apply_pending_music();                                   // the page is heard again: the music of what happened meanwhile starts now, once
    note_hidden_period();
}

// What the page's steps wanted of the music while nobody heard it (a match began, ended or was lost): the last wish is carried out once, by the next frame or when the
// page is shown. Nothing happened: nothing is touched, the piece that plays goes on.
void Application::apply_pending_music() {
    const PendingMusic what = pending_music_;
    pending_music_ = PendingMusic::None;
    switch (what) {
        case PendingMusic::InGame: play_next_ingame_music(); break;
        case PendingMusic::Intro: start_intro_music(); break;
        case PendingMusic::Closed: close_music(); break;
        case PendingMusic::None: break;
    }
}

std::string Application::hidden_period_line(double seconds, uint64_t ticks, uint32_t wakes, double seconds_since_last_line) {
    if (seconds < 1.0 || wakes == 0 || seconds_since_last_line < 10.0) return std::string();     // a short period (a tab that is flicked through) is not worth a line, nor is a stream of them
    const double shown = static_cast<int>(seconds * 10.0 + 0.5) / 10.0;
    std::string line = "The page was hidden for " + std::to_string(shown);
    line.resize(line.find('.') + 2);                         // one decimal
    line += " s; ";
    if (ticks > 0) line += "the match advanced by " + std::to_string(ticks) + " ticks and " + std::to_string(wakes) + " wake-ups stepped it in the background";
    else line += std::to_string(wakes) + " wake-ups kept the connection going in the background (no match tick ran)";
    return line;
}

// The page is shown again: the console gets one line about the hidden period (web build), unless it was short or another line came just before
void Application::note_hidden_period() {
    const double frequency = static_cast<double>(SDL_GetPerformanceFrequency());
    const double seconds = last_frame_time_ > hidden_since_ ? static_cast<double>(last_frame_time_ - hidden_since_) / frequency : 0.0;
    const double since_line = hidden_line_at_ != 0 && last_frame_time_ > hidden_line_at_ ? static_cast<double>(last_frame_time_ - hidden_line_at_) / frequency : 1.0e9;
    hidden_line_ = hidden_period_line(seconds, hidden_ticks_, hidden_wakes_, since_line);
    if (hidden_line_.empty()) return;
    hidden_line_at_ = last_frame_time_;
#if defined(__EMSCRIPTEN__)
    std::cout << "[Application] " << hidden_line_ << std::endl;
#endif
}

void Application::refresh_page_visibility() {
#if defined(__EMSCRIPTEN__)
    EmscriptenVisibilityChangeEvent status{};
    if (emscripten_get_visibility_status(&status) == EMSCRIPTEN_RESULT_SUCCESS) set_page_hidden(status.hidden != 0);
#endif
}

bool Application::background_pump() {
    refresh_page_visibility();                               // (what the browser says, not only what its events said)
    return background_step();
}

// Do the wake-ups drive the match now? The page is hidden, a room or a match exists, no step is running, and the frame loop is slower than the turns: a browser that still
// draws a hidden page often enough (an embedded browser pane that is not on screen) keeps the frames in charge, with picture and sound. A frame loop that comes less often than
// once per turn cannot carry the match alone (the server sends a turn every kTurnMs); the wake-ups step between its frames.
bool Application::wake_may_step() {
    if (!is_running_ || advancing_ || !background_driven()) return false;
    if (last_frame_run_ != 0) {
        const uint64_t now = now_counter();
        if (now >= last_frame_run_ && static_cast<double>(now - last_frame_run_) / static_cast<double>(SDL_GetPerformanceFrequency()) < kFrameAliveSeconds) return false;
        // (a clock that went backwards says nothing of when the last frame ran: the frames are not taken to be in charge, the hidden page is not left without a driver)
    }
    return true;
}

// The real time since the clocks were last advanced (a frame or the wake-up before): the same timer as the frame loop's, so that a frame after a wake-up measures from it
bool Application::background_step() {
    if (!wake_may_step()) return false;
    const uint64_t now = now_counter();
    const double elapsed = last_frame_time_ > 0 && now > last_frame_time_
                               ? static_cast<double>(now - last_frame_time_) / static_cast<double>(SDL_GetPerformanceFrequency())
                               : 0.0;
    last_frame_time_ = now;
    background_run(elapsed);
    return true;
}

bool Application::background_pump_after(float dt) {
    if (!wake_may_step()) return false;
    background_run(static_cast<double>(dt));
    return true;
}

// The network gets at most kMaxWakeSeconds of the time that passed (the lock-step runner must not be paid back more at once); the rest is a gap that only the host's silence
// counts (pump_network reads the connection first: what waited is not silence)
void Application::background_run(double elapsed) {
    const AdvanceGuard advance(advancing_);
    const AdvanceGuard silent(background_stepping_);         // (the ticks of this step make no sound, its events start no music)
    const double seconds = std::clamp(elapsed, 0.0, kMaxWakeSeconds);
    pump_network(static_cast<float>(seconds), elapsed > seconds ? elapsed - seconds : 0.0);    // the network clock, the room or the session, every tick that is due (post_tick: the HUD's state) and what the net reports
    ++background_pumps_;
    ++hidden_wakes_;
}

#if defined(__EMSCRIPTEN__)
namespace {
// What the frames cost, for the page's browser checks (ants_probe 12 - 15): the time that the game's own frame function takes (input, network, ticks, the picture), not what the browser does
// with the canvas afterwards
double g_frame_work_sum_ms = 0.0;
double g_frame_work_max_ms = 0.0;
uint32_t g_frame_work_frames = 0;
}  // namespace

extern "C" void emscripten_main_loop_iter(void* arg) {
    auto* app = static_cast<Application*>(arg);
    if (app && app->is_running()) {
        const double began = emscripten_get_now();
        app->run_frame();
        const double took = emscripten_get_now() - began;
        g_frame_work_sum_ms += took;
        g_frame_work_max_ms = std::max(g_frame_work_max_ms, took);
        ++g_frame_work_frames;
    }
}

// For the page (web/shell.html): its timer wakes a hidden page's network match when the server is quiet (every message of the server wakes it by itself, see
// NetGame::set_on_wake). It does nothing in a page that is shown, and nothing while a step is already running.
extern "C" EMSCRIPTEN_KEEPALIVE void ants_background_pump() {
    if (g_web_app != nullptr) g_web_app->background_pump();
}

// For the page (web/shell.html): the player has left a network match on purpose (the header's Menu button and the footer's Menu link, once the player has said yes):
// the game tells the server (Leave: the seat is dropped now, the others do not wait for it) and lets go of the key. The page navigates away right after. A closed tab and a
// reload do not call it: the seat is held and the key kept, so that the player can come back. The page calls it only once the game runs (isReadyToPlay).
extern "C" EMSCRIPTEN_KEEPALIVE void ants_leave_match() {
    if (g_web_app != nullptr) g_web_app->leave_network_match();
}

// For the page (web/shell.html): the browser took the touch away (touchcancel: a system gesture, a call, the lock screen) or the page knows that no finger of the game can be down (a first
// touch lands: whatever the game still tracks lost its lift): no finger is tracked any more and a press that a finger held ends with no act. The cancel waits behind the finger events that SDL
// already holds (a finger that went down in the same frame is cancelled after it begins). SDL turns a touchcancel into the finger's lift as well, which the touch model finds unknown by then.
extern "C" EMSCRIPTEN_KEEPALIVE void ants_touch_cancel() {
    if (g_web_app != nullptr) g_web_app->cancel_touch_queued();
}

// For the page (web/shell.html): the selector of the picture under the game changes the shape of the picture while the game runs (Application::set_aspect: no reload, no new match, nothing of the
// simulation is touched). `shape` is the place of the shape in the shape table (aspect_place / aspect_at_place in canvas_layout.hpp: the selector's order, left to right; a number, because the page's exports
// take only numbers). Returns 1 when the game has that shape now (also when it had it already), 0 for a number that is no shape (nothing changes), -1 when the game is not up yet (the page asks again
// when it is: its postRun hook, and the arguments that it starts with name the shape that the page has now).
extern "C" EMSCRIPTEN_KEEPALIVE int ants_set_aspect(int shape) {
    if (g_web_app == nullptr) return -1;
    Aspect aspect = Aspect::Classic4x3;
    if (!aspect_at_place(shape, aspect)) return 0;
    return g_web_app->set_aspect(aspect) ? 1 : -1;
}

// For the page (web/shell.html): 1 while a match is being played (the match screen is up and its results are not), else 0. The Menu button and the footer's link ask the player first when this says 1
// (the picture's selector does not: it changes the shape without leaving the match). A recording that is watched is no match to lose: 0.
extern "C" EMSCRIPTEN_KEEPALIVE int ants_match_running() {
    return (g_web_app != nullptr && !g_web_app->replay_mode() && g_web_app->match_running()) ? 1 : 0;
}

// For the page's replay bar (web/shell.html, docs/REPLAYS.md "Watching"): what the game shows of the recording. `what` is a ReplayValue (replay_view.hpp): the state, the turn, the turns of the recording, the
// speed times 100, how far a jump is, why the file cannot be shown, the turn of the jump. -1 when there is no game or it shows no replay.
extern "C" EMSCRIPTEN_KEEPALIVE int ants_replay_get(int what) {
    if (g_web_app == nullptr || !g_web_app->replay_mode()) return -1;
    return g_web_app->replay_value(static_cast<ants::app::ReplayValue>(what));
}

// ... and what the bar asks of it: a ReplayControl (replay_view.hpp) and its value (pause 0 or 1, the speed times 100, the turn to jump to)
extern "C" EMSCRIPTEN_KEEPALIVE void ants_replay_do(int what, int value) {
    if (g_web_app != nullptr && g_web_app->replay_mode()) g_web_app->replay_control(static_cast<ants::app::ReplayControl>(what), value);
}

// For the page's browser check (tests/scripts/web_edge_check.py): what the game believes about the pointer and the view, read-only. 0 and 1: the pointer's x and y on the picture (what the
// game draws its cursor at and scrolls from); 2: 1 while the game takes the pointer as gone from its window (no cursor, no scrolling); 3 and 4: the map view's origin in world pixels
// (-1 outside a match); 5: 1 while a dialog of the match is open (options, quit, quick help, the "get ready" dialog: the edges do not scroll then); 6: the view's zoom times 100.
// The prediction of one's own orders (docs/NETWORK_PORT.md), for the browser measurements: 7: the corner's "delay" in ms as the player reads it (the felt delay while the prediction is on, the
// network's otherwise; -1 for a dash); 8: the network's delay (the confirmed engine's) in ms, -1 when none is measured; 9: the prediction's state, 0 off, 1 on, 2 cooling down after its budget; 10: the
// orders that it has predicted; 11: the rebuilds that it has made (10 and 11 are 0 when the game has no prediction at all); 12 - 15: the frames' own work since the last reset, in microseconds (12: the mean, 13: the longest, 14: the number of frames;
// 15: reads 0 and starts again). The lines that the way-back overlay shows (the page cannot read the canvas; tests/scripts/web_rejoin_check.py): 16: how many lines there are now (0: none); 10000 + 1000 * line + index:
// the code of the character at `index` of that line (net_overlay_probe: 0 past its end, -1 for no such line).
// The seats of the match, for the page's browser check (tests/scripts/web_home_check.py): 20: the roster, bit s set when seat s plays (15: all four colonies, 1: a game for one) (-1 outside a match);
// 21 - 24: the ants of seat 0 - 3 that are alive now (-1 outside a match).
// The picture's shape, for the page's browser check (tests/scripts/web_aspect_check.py): 40: the canvas's width, 41: its height (SDL's logical size), 42: the shape (its place in the shape table, ants_set_aspect's number), 43: where the picture on screen starts, x, 44: y, 45: its width, 46: its height (a page in a bigger canvas is centred in it).
// The touch model, for the page's browser check (tests/scripts/web_touch_check.py): 30: the taps, 31: the holds, 32: the drags, 33: the two-finger gestures, 34: the fingers that are tracked, 35: what the model
// is doing (touch_control.hpp Mode: 0 idle, 1 waiting, 2 left button, 3 right button, 4 minimap, 5 two fingers), 36: the slop in picture pixels times 100.
// The picking, for the page's browser check of a replay (tests/scripts/web_camera_check.py): 50: the ants that are picked, plus 1000 when a base is picked and 100000 when an ant is inspected (0: nothing is picked; -1 outside a match).
// The countdown card (protocol 21), for the page's Fullscreen button (web/countdown_button.js): 60 - 65, see Application::countdown_probe (application_net.cpp).
// Anything else, or no game: -1.
extern "C" EMSCRIPTEN_KEEPALIVE int ants_probe(int what) {
    if (g_web_app == nullptr) return -1;
    switch (what) {
        case 0: return g_web_app->mouse_screen_x();
        case 1: return g_web_app->mouse_screen_y();
        case 2: return g_web_app->pointer_outside() ? 1 : 0;
        case 3: return g_web_app->match_running() ? g_web_app->renderer().camera().world_x : -1;
        case 4: return g_web_app->match_running() ? g_web_app->renderer().camera().world_y : -1;
        case 5: return g_web_app->match_running() ? (g_web_app->hud().is_modal_open() ? 1 : 0) : -1;
        case 6: return static_cast<int>(g_web_app->zoom() * 100.0f + 0.5f);
        case 7: {
            const std::optional<uint32_t> delay = g_web_app->corner_delay_ms();
            return delay ? static_cast<int>(*delay) : -1;
        }
        case 8: {
            const net::NetGame* game = g_web_app->net();
            const std::optional<uint32_t> delay = game != nullptr ? game->command_delay_ms() : std::nullopt;
            return delay ? static_cast<int>(*delay) : -1;
        }
        case 9: {
            const net::NetGame* game = g_web_app->net();
            if (game == nullptr) return 0;
            return game->prediction_cooling_down() ? 2 : (game->predicting() ? 1 : 0);
        }
        case 10: {                                                              // (no game: -1; a game that has no prediction (the default is off: it is never made) predicted nothing: 0)
            const net::NetGame* game = g_web_app->net();
            if (game == nullptr) return -1;
            return game->prediction() != nullptr ? static_cast<int>(game->prediction()->stats().commands_predicted) : 0;
        }
        case 11: {
            const net::NetGame* game = g_web_app->net();
            if (game == nullptr) return -1;
            return game->prediction() != nullptr ? static_cast<int>(game->prediction()->stats().rebuilds) : 0;
        }
        case 12: return g_frame_work_frames != 0 ? static_cast<int>(g_frame_work_sum_ms * 1000.0 / static_cast<double>(g_frame_work_frames)) : -1;
        case 13: return static_cast<int>(g_frame_work_max_ms * 1000.0);
        case 14: return static_cast<int>(g_frame_work_frames);
        case 15:
            g_frame_work_sum_ms = 0.0;
            g_frame_work_max_ms = 0.0;
            g_frame_work_frames = 0;
            return 0;
        case 20: return g_web_app->match_running() ? static_cast<int>(g_web_app->sim().roster_mask()) : -1;
        case 21: case 22: case 23: case 24: {
            if (!g_web_app->match_running()) return -1;
            int alive = 0;
            for (const ants::sim::AntSnapshot& ant : g_web_app->sim().get_world_state().ants) alive += ant.player_id == what - 21 ? 1 : 0;
            return alive;
        }
        case 40: return g_web_app->canvas().width;
        case 41: return g_web_app->canvas().height;
        case 42: return aspect_place(g_web_app->aspect());               // the shape's place in the shape table (ants_set_aspect's number)
        case 43: return g_web_app->picture().x;
        case 44: return g_web_app->picture().y;
        case 45: return g_web_app->picture().w;
        case 46: return g_web_app->picture().h;
        case 30: return static_cast<int>(g_web_app->touch().stats().taps);
        case 31: return static_cast<int>(g_web_app->touch().stats().holds);
        case 32: return static_cast<int>(g_web_app->touch().stats().drags);
        case 33: return static_cast<int>(g_web_app->touch().stats().two_finger);
        case 34: return static_cast<int>(g_web_app->touch().fingers());
        case 35: return static_cast<int>(g_web_app->touch().mode());
        case 36: return static_cast<int>(g_web_app->touch_slop() * 100.0 + 0.5);
        case 60: case 61: case 62: case 63: case 64: case 65: return g_web_app->countdown_probe(what);                // (the countdown card, protocol 21: application_net.cpp)
        case 50: return g_web_app->match_running() ? static_cast<int>(g_web_app->hud().get_selected_ant_ids().size()) + (g_web_app->hud().get_selected_base_team_id() >= 0 ? 1000 : 0) + (g_web_app->hud().get_selected_ant_id() != 0 ? 100000 : 0) : -1;
        default: return net_overlay_probe(g_web_app->net_overlay_now(), what);                 // (16 and 10000 and up; anything else is -1)
    }
}
#endif

int Application::run() {
    last_frame_time_ = SDL_GetPerformanceCounter();
    headless_frame_count_ = 0;

#if defined(__EMSCRIPTEN__)
    // 0 fps synchronizes with requestAnimationFrame, 1 simulates infinite loop
    emscripten_set_main_loop_arg(emscripten_main_loop_iter, this, 0, 1);
    return 0;
#else
    uint64_t perf_freq = SDL_GetPerformanceFrequency();

    while (is_running_) {
        uint64_t current_time = SDL_GetPerformanceCounter();
        float delta_time = static_cast<float>(current_time - last_frame_time_) / static_cast<float>(perf_freq);
        last_frame_time_ = current_time;

        run_frame_with_delta(delta_time);

        // 60 FPS target frame limiter (~16.666 ms per frame)
        if (!config_.headless) {
            uint64_t frame_end = SDL_GetPerformanceCounter();
            float elapsed_ms = (static_cast<float>(frame_end - current_time) * 1000.0f) / static_cast<float>(perf_freq);
            if (elapsed_ms < 16.666f) {
                SDL_Delay(static_cast<uint32_t>(16.666f - elapsed_ms));
            }
        }
    }
    return 0;
#endif
}

} // namespace ants::app
