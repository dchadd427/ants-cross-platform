#include "ants_app/application.hpp"
#include "ants_app/ui_anim.hpp"
#include "ants_app/version.hpp"
#include <iostream>
#include <cstring>
#include <algorithm>
#include <cstdlib>
#include <random>
#if defined(_WIN32)
  #include <winsock2.h>
  #include <windows.h>
#elif defined(__EMSCRIPTEN__)
  #include <emscripten.h>
  #include <unistd.h>
#else
  #include <unistd.h>
#endif

namespace ants::app {

namespace {

std::string get_system_username() {
#if defined(__EMSCRIPTEN__)
    return "WebPlayer";
#else
    const char* user = std::getenv("USER");
    if (!user) user = std::getenv("USERNAME");
    std::string u = (user && *user) ? user : "Player";
    char host[256] = {0};
    if (gethostname(host, sizeof(host) - 1) == 0 && host[0]) {
        std::string h(host);
        size_t dot = h.find('.');
        if (dot != std::string::npos) {
            h = h.substr(0, dot);
        }
        return u + "@" + h;
    }
    return u;
#endif
}

} // anonymous namespace

Application::Application() = default;

Application::~Application() {
    shutdown();
}

bool Application::init(int argc, char* argv[]) {
    return init(parse_arguments(argc, argv));
}

ApplicationConfig Application::parse_arguments(int argc, char* argv[]) {
    ApplicationConfig cfg{};

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--headless") == 0) {
            cfg.headless = true;
        } else if (std::strcmp(argv[i], "--map") == 0 && i + 1 < argc) {
            cfg.default_map_path = argv[++i];
            cfg.start_in_map_select = false;
        } else if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            cfg.random_seed = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (std::strcmp(argv[i], "--fullscreen") == 0) {
            cfg.fullscreen = true;
        } else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            cfg.screenshot_path = argv[++i];
        } else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            cfg.screenshot_frames = std::stoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--select-ant") == 0 && i + 1 < argc) {
            cfg.select_ant_id = std::stoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--select-base") == 0 && i + 1 < argc) {
            cfg.select_base_team = std::stoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--open-options") == 0) {
            cfg.open_options = true;
            cfg.start_in_map_select = false;
        } else if (std::strcmp(argv[i], "--show-grid") == 0) {
            cfg.show_tile_grid = true;
        } else if (std::strcmp(argv[i], "--map-select") == 0) {
            cfg.start_in_map_select = true;
        } else if (std::strcmp(argv[i], "--player") == 0 && i + 1 < argc) {
            cfg.local_player_id = static_cast<uint8_t>(std::stoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--scorecard") == 0) {
            cfg.show_scorecard = true;
            cfg.start_in_map_select = false;
        } else if (std::strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
            cfg.player_name = argv[++i];                                       // this player's name: the room, the HUD, chat, the results
        } else if (std::strcmp(argv[i], "--team-name") == 0 && i + 2 < argc) {   // --team-name <0-3> <name>, for a local game
            const int team = std::atoi(argv[i + 1]);
            if (team >= 0 && team < 4) cfg.team_names[static_cast<size_t>(team)] = argv[i + 2];
            i += 2;
        } else if (argv[i][0] == '-' && argv[i][1] == 'N' && argv[i][2] >= '0' && argv[i][2] <= '3') {
            cfg.team_names[static_cast<size_t>(argv[i][2] - '0')] = argv[i] + 3;   // the original's -N<team><name>
        } else if (std::strncmp(argv[i], "-pnum=", 6) == 0) {
            const int team = std::atoi(argv[i] + 6);                           // the original's local team
            if (team >= 0 && team < 4) cfg.local_player_id = static_cast<uint8_t>(team);
        } else if (std::strcmp(argv[i], "--host") == 0) {
            cfg.net_role = ApplicationConfig::NetRole::Host;
            if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9') {   // an optional port
                cfg.net_port = static_cast<uint16_t>(std::stoul(argv[++i]));
            }
        } else if (std::strcmp(argv[i], "--join") == 0 && i + 1 < argc) {
            cfg.net_role = ApplicationConfig::NetRole::Join;
            std::string target = argv[++i];                                    // host or host:port
            const size_t colon = target.rfind(':');
            if (colon != std::string::npos && target.find(':') == colon) {
                cfg.net_port = static_cast<uint16_t>(std::stoul(target.substr(colon + 1)));
                target.resize(colon);
            }
            cfg.net_address = target;
        } else if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            cfg.net_port = static_cast<uint16_t>(std::stoul(argv[++i]));
        } else if (std::strcmp(argv[i], "--loopback") == 0) {
            cfg.net_loopback_only = true;
        }
    }
    return cfg;
}

bool Application::init(const ApplicationConfig& config) {
    config_ = config;
    show_tile_grid_ = config_.show_tile_grid;

    // 1. Initialize SDL2
    uint32_t sdl_flags = SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER;
    if (config_.headless) {
        SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
    }

    if (SDL_Init(sdl_flags) != 0) {
        std::cerr << "[Application] SDL_Init Error: " << SDL_GetError() << std::endl;
        return false;
    }

    // 2. Load Master Asset Archive (ants.chd)
    if (!assets_.load_from_file(config_.chd_path)) {
        std::cerr << "[Application] Failed to load CHD archive: " << config_.chd_path << std::endl;
        return false;
    }

    // 3. Load Map Level
    if (!current_level_.load_from_file(config_.default_map_path)) {
        std::cerr << "[Application] Failed to load Level: " << config_.default_map_path << std::endl;
        return false;
    }

    // 4. Initialize Simulation Engine
    sim_.init(current_level_, config_.random_seed);

    // 5. Initialize Audio Subsystem
    audio_mixer_.set_headless_mode(config_.headless);
    audio_mixer_.init(assets_);
    if (!config_.headless) {
        audio_mixer_.init_sdl_audio(1024);
    }

    midi_player_.set_headless_mode(config_.headless);
    midi_player_.load_file(config_.midi_path);

    // 6. Create Desktop Window
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
        std::cerr << "[Application] Failed to create SDL Window: " << SDL_GetError() << std::endl;
        return false;
    }

    if (!config_.headless) {
        SDL_RaiseWindow(window_);
        SDL_ShowCursor(SDL_DISABLE);
    }

    // 7. Initialize Renderer
    renderer_ = std::make_unique<Renderer>();
    if (!renderer_->init(window_, assets_, config_.integer_scaling)) {
        std::cerr << "[Application] Failed to initialize Renderer" << std::endl;
        return false;
    }

    renderer_->set_level(current_level_);

    // Center camera on current player's base spawn (center of 4x4 anthill structure)
    const auto* base = sim_.grid().find_anthill(local_player_id_);
    if (base) {
        renderer_->camera().center_on(base->x * TILE_SIZE + 2 * TILE_SIZE,
                                      base->y * TILE_SIZE + 2 * TILE_SIZE,
                                      current_level_.width, current_level_.height);
    } else if (!current_level_.anthill_spawns.empty()) {
        const auto& spawn = current_level_.anthill_spawns[0];
        renderer_->camera().center_on(spawn.x * TILE_SIZE + 2 * TILE_SIZE,
                                      spawn.y * TILE_SIZE + 2 * TILE_SIZE,
                                      current_level_.width, current_level_.height);
    }

    // 8. Initialize HUD and Scorecard
    hud_.init(0);
    local_player_id_ = 0;

    scorecard_.set_on_replay([this]() {
        if (network_active()) {                                       // a network match is not replayed here: back to the setup screen
            return_to_map_select();
            return;
        }
        scorecard_.hide();
        sim_.init(current_level_, config_.random_seed + 1);
        match_over_handled_ = false;
        hud_.reset();
        midi_player_.stop(); // In-game music stays silent
    });

    scorecard_.set_on_quit([this]() {
        quit();
    });
    scorecard_.set_on_play_sfx([this](uint32_t sound_id) {
        audio_mixer_.play_sfx(sound_id, 1.0f, 255);
    });

    hud_.set_sim_query(&sim_);           // the cursor asks the simulation whether a tile is a valid special target
    hud_.set_on_quit([this]() {
        quit();
    });

    hud_.set_on_sfx_volume([this](float v) {
        audio_mixer_.set_sfx_volume(v);
    });

    hud_.set_on_music_volume([this](float v) {
        audio_mixer_.set_music_volume(v);
        midi_player_.set_volume(v);
    });

    hud_.set_on_play_sfx([this](uint32_t sound_id) {
        audio_mixer_.play_sfx(sound_id, 1.0f, 255);
    });

    hud_.set_on_spawn_click_marker([this](int32_t wx, int32_t wy) {
        if (renderer_) {
            renderer_->spawn_transient_effect("xmarks", wx, wy);
        }
    });

    audio_mixer_.set_sfx_volume(hud_.get_sfx_volume());
    audio_mixer_.set_music_volume(hud_.get_music_volume());
    midi_player_.set_volume(hud_.get_music_volume());

    // 9. Initialize Map Selection Screen
    const bool networked = config_.net_role != ApplicationConfig::NetRole::None;
    const size_t my_team = config_.local_player_id < 4 ? config_.local_player_id : 0u;
    // The name of this player: --name, else the -N name of its team, else the system user (a network game never sends the user and machine name
    // by default: it says "Player" unless --name is given)
    std::string player_name = !config_.player_name.empty() ? config_.player_name
                              : (!config_.team_names[my_team].empty() ? config_.team_names[my_team]
                                                                       : (networked ? std::string("Player") : get_system_username()));
    player_name_ = player_name;
    map_select_.init("Original-Ants/Maps");
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
        if (net_ && net_->is_host()) net_->set_fog(fog);
    });
    map_select_.set_on_quit([this]() {
        quit();
    });
    map_select_.set_on_play_sfx([this](uint32_t sound_id) {
        audio_mixer_.play_sfx(sound_id, 1.0f, 255);
    });

    // A network game: the room is the setup screen (host: pick the map and START; guest: follow the host's choice)
    if (networked) {
        config_.start_in_map_select = true;
        net_ = std::make_unique<net::NetGame>(sim_);
        const bool ok = config_.net_role == ApplicationConfig::NetRole::Host
                            ? net_->host(config_.net_port, player_name, config_.net_loopback_only)
                            : net_->join(config_.net_address, config_.net_port, player_name);
        if (!ok) {
            std::cerr << "[Application] Could not " << (config_.net_role == ApplicationConfig::NetRole::Host ? "open a room on port " : "reach the host at ")
                      << (config_.net_role == ApplicationConfig::NetRole::Host ? std::to_string(config_.net_port) : config_.net_address + ":" + std::to_string(config_.net_port))
                      << std::endl;
            return false;
        }
        net_->set_on_tick([this]() { post_tick(); });
        net_->set_on_chat([this](const net::ChatMsg& m) {
            if (m.sender == local_player_id_ || m.sender >= 4) return;                // the own text is in the log already
            hud_.receive_chat_message(m.sender, sim_.get_player_name(m.sender), m.text, m.team, sim_.get_world_state());
        });
        hud_.set_on_chat_send([this](const std::string& text, bool team) {
            if (network_active()) net_->chat(text, team);
        });
        if (net_->is_host() && !map_select_.get_maps().empty()) net_->set_map(map_select_.get_maps()[static_cast<size_t>(map_select_.get_selected_index())].filename);
        sync_room_view();
    } else {
        apply_team_names(config_.team_names, 0x0F);                                    // the names of a local game (the local player's own name too)
    }

    // Determine initial AppState & audio lifecycle
    if (!config_.start_in_map_select) {
        state_ = AppState::Playing;
        audio_mixer_.stop_music();
        midi_player_.stop();
        if (config_.select_ant_id > 0) {
            hud_.select_ant(static_cast<uint32_t>(config_.select_ant_id));
            if (renderer_) {
                for (const auto& a : sim_.get_world_state().ants) {
                    if (a.id == static_cast<uint32_t>(config_.select_ant_id)) {
                        renderer_->camera().center_on(a.px, a.py, current_level_.width, current_level_.height);
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
            sim::MatchResult mr{};
            mr.is_over = true;
            mr.winning_players = {0};
            mr.final_scores = {1500, 850, 420, 100};
            mr.stats[0] = {5, 18, 25};
            mr.stats[1] = {12, 8, 15};
            mr.stats[2] = {20, 4, 10};
            mr.stats[3] = {25, 1, 5};
            scorecard_.show(mr, 0);
        }
    } else {
        if (!config_.skip_intro && !config_.headless) {
            state_ = AppState::Loading;
            intro_ticks_ = 0;
        } else {
            state_ = AppState::MapSelect;
        }
        audio_mixer_.play_music("Original-Ants/INTRO.mp3", true);
        if (config_.headless) {
            midi_player_.play(true); // Loop INTRO exclusively during map selection
        }
    }

    is_running_ = true;
    last_tick_time_ = SDL_GetPerformanceCounter();
    frametime_history_.fill(16.666f);
    frametime_index_ = 0;
    fps_display_value_ = 60.0f;
    fps_time_accumulator_ = 0.0f;
    fps_frame_counter_ = 0;
    mouse_screen_x_ = 320;
    mouse_screen_y_ = 240;
    mouse_has_moved_ = false;
    return true;
}

void Application::shutdown() {
    is_running_ = false;
    if (net_) net_->leave();                              // the others see a clean goodbye, not a dead connection

    midi_player_.shutdown();
    audio_mixer_.shutdown_sdl_audio();

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

bool Application::start_game(const std::string& map_path) {
    if (!load_match(map_path, config_.random_seed, 0x0F, map_select_.is_fog_of_war_enabled())) return false;
    apply_team_names(config_.team_names, 0x0F);
    enter_match();
    return true;
}

// The level, the simulation and the renderer of a match (the map file, the seed, the teams that play and the Fog of War option are the whole
// shared state: every machine of a network match calls this with the same values).
bool Application::load_match(const std::string& map_path, uint32_t seed, uint8_t roster, bool fog) {
    if (!current_level_.load_from_file(map_path)) {
        std::cerr << "[Application] Failed to load level: " << map_path << std::endl;
        return false;
    }
    config_.default_map_path = map_path;
    if ((roster & 0x0Fu) != 0x0Fu) current_level_ = current_level_.for_roster(roster);      // no hill art for a team without a player

    sim_.set_fog_of_war_enabled(fog);
    sim_.set_viewing_player_id(local_player_id_);
    sim_.init(current_level_, seed, roster);

    if (renderer_) {
        renderer_->set_level(current_level_);
        const auto* base = sim_.grid().find_anthill(local_player_id_);
        if (base) {
            renderer_->camera().center_on(base->x * TILE_SIZE + 2 * TILE_SIZE,
                                          base->y * TILE_SIZE + 2 * TILE_SIZE,
                                          current_level_.width, current_level_.height);
        } else if (!current_level_.anthill_spawns.empty()) {
            const auto& spawn = current_level_.anthill_spawns[0];
            renderer_->camera().center_on(spawn.x * TILE_SIZE + 2 * TILE_SIZE,
                                          spawn.y * TILE_SIZE + 2 * TILE_SIZE,
                                          current_level_.width, current_level_.height);
        }
    }
    hud_.set_roster_mask(roster);
    return true;
}

// The moment a match starts for the player: music and the start sound, the HUD, the "get ready" modal, the Playing state.
void Application::enter_match() {
    // In-Game Music: Shuffle between ANTS2A, ANTS2B, ANTSFUN3
    if (!is_music_muted_) {
        play_next_ingame_music();
    } else {
        audio_mixer_.stop_music();
        midi_player_.stop();
    }

    // Play authentic random game startup sound (rndm1..6.wav / Sound IDs 7..12)
    play_startup_sound();

    // Reset HUD & Scorecard
    hud_.init(local_player_id_);
    hud_.reset();
    hud_.start_match_modal();
    scorecard_.hide();
    match_over_handled_ = false;

    // Reset simulated cursor to middle of screen until actually seen moving
    mouse_screen_x_ = 320;
    mouse_screen_y_ = 240;
    mouse_has_moved_ = false;

    if (config_.select_ant_id > 0) {
        hud_.select_ant(static_cast<uint32_t>(config_.select_ant_id));
        if (renderer_) {
            for (const auto& a : sim_.get_world_state().ants) {
                if (a.id == static_cast<uint32_t>(config_.select_ant_id)) {
                    renderer_->camera().center_on(a.px, a.py, current_level_.width, current_level_.height);
                    break;
                }
            }
        }
    } else if (config_.select_base_team >= 0) {
        hud_.select_base(config_.select_base_team);
    }

    state_ = AppState::Playing;
}

// The names of the teams reach every place that shows one: the simulation's texts (alliances, drop-outs, the chat log), the HUD's score labels,
// the results rows and the local player's own label. An empty name keeps the colour word.
void Application::apply_team_names(const std::array<std::string, 4>& names, uint8_t roster) {
    std::array<std::string, 4> all = names;
    if (!network_active() && local_player_id_ < 4) all[local_player_id_] = player_name_;      // a local game: the own name (--name) belongs to the own team
    for (uint8_t p = 0; p < 4; ++p) {
        sim_.set_player_name(p, ((roster >> p) & 1u) != 0 ? all[p] : std::string());       // an empty name is the colour word again
    }
    hud_.set_team_names(all);
    scorecard_.set_player_names(all);
    if (local_player_id_ < 4 && !all[local_player_id_].empty()) {
        hud_.set_player_name(all[local_player_id_]);
        scorecard_.set_local_player_name(all[local_player_id_]);
    }
}

void Application::quit() {
    if (net_) net_->leave();
#if defined(__EMSCRIPTEN__)
    if (state_ == AppState::Playing || scorecard_.is_open()) {
        return_to_map_select();
    } else {
        emscripten_run_script("window.location.reload();");
    }
#else
    is_running_ = false;
#endif
}

void Application::return_to_map_select() {
    if (network_active()) net_end_session(net_notice_);   // leaving a match leaves the room: the local setup screen follows
    net_notice_.clear();
    state_ = AppState::MapSelect;
    scorecard_.hide();
    hud_.close_quit_dialog();
    hud_.close_quick_help();
    hud_.close_options();
    mouse_screen_x_ = 320;
    mouse_screen_y_ = 240;
    mouse_has_moved_ = false;
    midi_player_.load_file(config_.midi_path);
    if (!is_music_muted_) {
        audio_mixer_.play_music("Original-Ants/INTRO.mp3", true);
        if (config_.headless) {
            midi_player_.play(true); // Resumes INTRO during map selection
        }
    }
}

void Application::run_frame_with_delta(float delta_time) {
    if (!is_running_) return;

    // Clamp delta_time to prevent physics / tick spiral when tab or window is backgrounded
    if (delta_time > 0.100f) {
        delta_time = 0.100f;
    }

    if (delta_time > 0.0001f) {
        float instant_fps = 1.0f / delta_time;
        current_fps_ = current_fps_ * 0.9f + instant_fps * 0.1f;

        // Record frametime in ms for sparkline
        float frame_ms = delta_time * 1000.0f;
        frametime_history_[frametime_index_] = frame_ms;
        frametime_index_ = (frametime_index_ + 1) % SPARKLINE_SAMPLES;

        // Rolling average: update displayed FPS every 250ms (~4 times/sec) for clear, stable readability
        fps_time_accumulator_ += delta_time;
        fps_frame_counter_++;
        if (fps_time_accumulator_ >= 0.25f) {
            fps_display_value_ = static_cast<float>(fps_frame_counter_) / fps_time_accumulator_;
            fps_time_accumulator_ = 0.0f;
            fps_frame_counter_ = 0;
        }
    }

    handle_events();

    handle_camera_panning(delta_time);

    pump_network(delta_time);                   // a network match has no pause: it keeps running while the window is in the background

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
    uint64_t current_time = SDL_GetPerformanceCounter();
    float delta_time = (last_frame_time_ > 0) ? (static_cast<float>(current_time - last_frame_time_) / static_cast<float>(perf_freq)) : 0.01666f;
    last_frame_time_ = current_time;

    run_frame_with_delta(delta_time);
}

#if defined(__EMSCRIPTEN__)
extern "C" void emscripten_main_loop_iter(void* arg) {
    auto* app = static_cast<Application*>(arg);
    if (app && app->is_running()) {
        app->run_frame();
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

void Application::handle_events() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            quit();
            return;
        }

        if (event.type == SDL_WINDOWEVENT) {
            if (event.window.event == SDL_WINDOWEVENT_MINIMIZED ||
                event.window.event == SDL_WINDOWEVENT_HIDDEN) {
                is_paused_ = true;
                midi_player_.pause();
            }
            if (event.window.event == SDL_WINDOWEVENT_RESTORED ||
                event.window.event == SDL_WINDOWEVENT_SHOWN) {
                is_paused_ = false;
                midi_player_.resume();
            }
            if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                event.window.event == SDL_WINDOWEVENT_RESIZED ||
                event.window.event == SDL_WINDOWEVENT_MAXIMIZED ||
                event.window.event == SDL_WINDOWEVENT_RESTORED) {
                if (renderer_ && window_) {
                    uint32_t flags = SDL_GetWindowFlags(window_);
                    bool is_fs = (flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_MAXIMIZED)) != 0;
                    config_.fullscreen = (flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) != 0;
                    renderer_->set_fullscreen(is_fs);
                }
            }
        }

        // Bypass Print Screen key so OS handles screenshots unimpeded
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_PRINTSCREEN) {
            continue;
        }

        // Global Fullscreen hotkeys: F11 (outside gameplay), Alt+Enter, or Cmd+F
        if (event.type == SDL_KEYDOWN) {
            bool is_f11 = (event.key.keysym.sym == SDLK_F11 && state_ != AppState::Playing);
            bool is_alt_enter = ((event.key.keysym.mod & KMOD_ALT) != 0 &&
                                 (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_KP_ENTER));
            bool is_cmd_f = ((event.key.keysym.mod & KMOD_GUI) != 0 &&
                             (event.key.keysym.sym == SDLK_f));
            if (is_f11 || is_alt_enter || is_cmd_f) {
                toggle_fullscreen();
                continue;
            }
        }

        if (state_ == AppState::Loading) {
            if (event.type == SDL_KEYDOWN || event.type == SDL_MOUSEBUTTONDOWN) {
                state_ = hud_.is_quick_help_enabled() ? AppState::QuickHelp : AppState::MapSelect;
            }
            continue;
        }

        if (state_ == AppState::QuickHelp) {
            if (event.type == SDL_MOUSEMOTION) {
                mouse_screen_x_ = event.motion.x;
                mouse_screen_y_ = event.motion.y;
                mouse_has_moved_ = true;
                quick_help_start_hovered_ = (event.motion.x >= 528 && event.motion.x <= 528 + 98 && event.motion.y >= 437 && event.motion.y <= 437 + 27);
            } else if (event.type == SDL_MOUSEBUTTONDOWN) {
                mouse_screen_x_ = event.button.x;
                mouse_screen_y_ = event.button.y;
                mouse_has_moved_ = true;
                if (event.button.x >= 528 && event.button.x <= 528 + 98 && event.button.y >= 437 && event.button.y <= 437 + 27) {
                    quick_help_start_pressed_ = true;
                }
            } else if (event.type == SDL_MOUSEBUTTONUP) {
                if (quick_help_start_pressed_) {
                    quick_help_start_pressed_ = false;
                    if (event.button.x >= 528 && event.button.x <= 528 + 98 && event.button.y >= 437 && event.button.y <= 437 + 27) {
                        state_ = AppState::MapSelect;   // qh_start3 carries no sound
                    }
                }
            } else if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_SPACE || event.key.keysym.sym == SDLK_ESCAPE) {
                    state_ = AppState::MapSelect;
                }
            }
            continue;
        }

        if (state_ == AppState::MapSelect) {
            switch (event.type) {
                case SDL_KEYDOWN:
                    map_select_.handle_key_down(event.key.keysym.sym);
                    break;
                case SDL_MOUSEMOTION:
                    mouse_screen_x_ = event.motion.x;
                    mouse_screen_y_ = event.motion.y;
                    mouse_has_moved_ = true;
                    map_select_.handle_mouse_motion(event.motion.x, event.motion.y);
                    break;
                case SDL_MOUSEBUTTONDOWN:
                    mouse_screen_x_ = event.button.x;
                    mouse_screen_y_ = event.button.y;
                    mouse_has_moved_ = true;
                    map_select_.handle_mouse_down(event.button.x, event.button.y, event.button.button);
                    break;
                case SDL_MOUSEBUTTONUP:
                    mouse_screen_x_ = event.button.x;
                    mouse_screen_y_ = event.button.y;
                    mouse_has_moved_ = true;
                    map_select_.handle_mouse_up(event.button.x, event.button.y, event.button.button);
                    break;
                default:
                    break;
            }
            continue;
        }

        // Gameplay Event Dispatch
        switch (event.type) {
            case SDL_TEXTINPUT:
                if (state_ == AppState::Playing) {
                    hud_.handle_text_input(event.text.text);
                }
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
            case SDL_MOUSEWHEEL:
                if (state_ == AppState::Playing) {
                    hud_.handle_mouse_wheel(mouse_screen_x_, mouse_screen_y_, event.wheel.y);
                }
                break;
            default:
                break;
        }
    }

}

// The INPUT task of the original runs every 50 ms (Ants.exe 0x100ae26): the edge strips and the minimap drag move the view in whole pixel
// steps at that rate (edge_scroll.hpp). Panning is strictly mouse-driven; keyboard keys do not scroll.
void Application::handle_camera_panning(float dt) {
    if (state_ == AppState::MapSelect || scorecard_.is_open()) return;
    input_accumulator_ += dt;
    while (input_accumulator_ >= 0.050f) {
        input_accumulator_ -= 0.050f;
        if (state_ != AppState::Playing || !renderer_ || !mouse_has_moved_) continue;
        if (mouse_screen_x_ < 0 || mouse_screen_x_ >= 640 || mouse_screen_y_ < 0 || mouse_screen_y_ >= 480) continue;
        hud_.input_tick(renderer_->camera(), current_level_.width, current_level_.height, mouse_screen_x_, mouse_screen_y_);
    }
}

void Application::handle_key_down(const SDL_KeyboardEvent& key) {
    if (scorecard_.is_open()) {
        if (key.keysym.sym == SDLK_ESCAPE) {
            return_to_map_select();
        }
        return;
    }

    bool ctrl_or_gui = (key.keysym.mod & KMOD_CTRL) || (key.keysym.mod & KMOD_GUI);

    // Developer and test shortcuts that are not part of the original (each needs Ctrl / Cmd, Shift or a function key that the original leaves
    // free): screenshot, tile grid, music mute, team switch. The original's own keys live in HUD::handle_key_down (FUN_0102609a).
    if (key.keysym.sym == SDLK_F12 && ((key.keysym.mod & KMOD_SHIFT) || ctrl_or_gui)) {
        renderer_->save_screenshot("screenshot.png");
        std::cout << "[Application] Screenshot saved to screenshot.png" << std::endl;
        return;
    }
    if ((key.keysym.sym == SDLK_t && ctrl_or_gui) || key.keysym.sym == SDLK_F3) {
        show_tile_grid_ = !show_tile_grid_;
        return;
    }
    if (key.keysym.sym == SDLK_m && ctrl_or_gui) {
        is_music_muted_ = !is_music_muted_;
        if (is_music_muted_) {
            audio_mixer_.stop_music();
            midi_player_.stop();
        } else {
            if (state_ == AppState::Playing) {
                play_next_ingame_music();
            } else {
                audio_mixer_.play_music("Original-Ants/INTRO.mp3", true);
                if (config_.headless) midi_player_.play(true);
            }
        }
        return;
    }
    if (ctrl_or_gui && !network_active()) {
        if (key.keysym.sym >= SDLK_1 && key.keysym.sym <= SDLK_4) {
            uint8_t target_team = static_cast<uint8_t>(key.keysym.sym - SDLK_1);
            set_local_player(target_team);
            return;
        }
        if (key.keysym.sym == SDLK_TAB || key.keysym.sym == SDLK_c) {
            uint8_t next_team = static_cast<uint8_t>((local_player_id_ + 1) % 4);
            set_local_player(next_team);
            return;
        }
    }

    // Printable keys reach the always-active chat box as text input events (SDL_TEXTINPUT); the key event only carries the control keys. A dialog
    // reads the keys itself (Y / N of the quit dialog, C / X of the quick help).
    if (!ctrl_or_gui && key.keysym.sym >= 32 && key.keysym.sym <= 126 && !hud_.is_modal_open()) {
        return;
    }

    hud_.handle_key_down(key.keysym.sym, sim_, renderer_->camera(), key.keysym.mod, key.repeat != 0);
}

void Application::handle_mouse_motion(const SDL_MouseMotionEvent& motion) {
    mouse_screen_x_ = motion.x;
    mouse_screen_y_ = motion.y;
    mouse_has_moved_ = true;

    if (scorecard_.is_open()) {
        scorecard_.handle_mouse_motion(motion.x, motion.y);
        return;
    }

    hud_.handle_mouse_motion(motion.x, motion.y, sim_, renderer_->camera());
}

void Application::handle_mouse_button(const SDL_MouseButtonEvent& button) {
    mouse_screen_x_ = button.x;
    mouse_screen_y_ = button.y;
    mouse_has_moved_ = true;

    if (scorecard_.is_open()) {
        if (button.type == SDL_MOUSEBUTTONDOWN) {
            scorecard_.handle_mouse_down(button.x, button.y);
        } else if (button.type == SDL_MOUSEBUTTONUP) {
            scorecard_.handle_mouse_up(button.x, button.y);
        }
        return;
    }

    uint16_t mod = static_cast<uint16_t>(SDL_GetModState());
    if (button.type == SDL_MOUSEBUTTONDOWN) {
        hud_.handle_mouse_down(button.x, button.y, button.button, sim_, renderer_->camera(), mod);
    } else if (button.type == SDL_MOUSEBUTTONUP) {
        hud_.handle_mouse_up(button.x, button.y, button.button, sim_, renderer_->camera(), mod);
    }
}

void Application::update_simulation(float dt) {
    if (state_ == AppState::Loading) {
        tick_accumulator_ += dt;
        while (tick_accumulator_ >= 0.050f) {
            intro_ticks_++;
            tick_accumulator_ -= 0.050f;
            if (intro_ticks_ >= 30) {
                state_ = hud_.is_quick_help_enabled() ? AppState::QuickHelp : AppState::MapSelect;
                break;
            }
        }
        return;
    }
    if (state_ == AppState::QuickHelp) {
        return;
    }

    if (state_ == AppState::MapSelect || is_paused_) {
        if (state_ == AppState::MapSelect) {
            midi_player_.update(dt);
        }
        return;
    }

    if (network_active() && net_->phase() == net::NetGame::Phase::Playing) {
        // The ticks come from the lock-step runner (pump_network); only what shows between two ticks is advanced here
        tick_accumulator_ = static_cast<float>(net_->sub_tick_ms()) / 1000.0f;
    } else {
        tick_accumulator_ += dt;
        while (tick_accumulator_ >= 0.050f) {
            if (!sim_.is_match_over()) {
                sim_.tick();
                post_tick();
            }
            tick_accumulator_ -= 0.050f;
        }
    }

    // Update spatial audio listener position
    audio_mixer_.set_listener_position(renderer_->camera().world_x + PLAYFIELD_W / 2,
                                       renderer_->camera().world_y + PLAYFIELD_H / 2);

    if (renderer_) {
        renderer_->update_transient_effects(dt);
    }

    audio_mixer_.update_music(dt);
    midi_player_.update(dt);
    if (state_ == AppState::Playing && !is_music_muted_ && !audio_mixer_.is_music_playing() && !midi_player_.is_playing() && !sim_.is_match_over()) {
        play_next_ingame_music();
    }
}

// What one simulation tick shows: the HUD, the events of the tick, the sounds and, once, the end of the match.
void Application::post_tick() {
    const auto& world = sim_.get_world_state();
    hud_.update(world, 1);
    hud_.poll_sim_events(sim_);

    auto audio_events = sim_.poll_audio_events();
    audio_mixer_.ingest_simulation_events(audio_events, local_player_id_);

    if (sim_.is_match_over() && !match_over_handled_) {
        match_over_handled_ = true;
        scorecard_.show(world.match_result, local_player_id_);
        uint32_t sting_sound = scorecard_.get_audio_to_play();
        if (sting_sound > 0) {
            audio_mixer_.play_sfx(sting_sound, 1.0f, 255);
            scorecard_.clear_audio_to_play();
        }
        audio_mixer_.fade_out_music(1.0f);
        midi_player_.fade_out(1.0f);
        if (net_) net_->freeze();                                    // the host stops sealing turns
    }
}

// ------------------------------------------------------------------------------------------------
// Network play (docs/NETWORK_PORT.md): the setup screen is the room, the lock-step runner drives the ticks
// ------------------------------------------------------------------------------------------------

void Application::pump_network(float dt) {
    if (!net_ || !net_->active()) return;
    net_time_ms_ += static_cast<double>(dt) * 1000.0;
    net_->update(static_cast<uint32_t>(net_time_ms_));
    handle_net_events();
    if (state_ == AppState::MapSelect && net_->active()) sync_room_view();
}

void Application::sync_room_view() {
    if (!net_ || !net_->active()) return;
    MapSelectScreen::RoomView view;
    view.networked = true;
    view.is_host = net_->is_host();
    view.my_seat = net_->my_seat();
    const net::RoomMsg& room = net_->room();
    for (size_t i = 0; i < view.seats.size(); ++i) {
        view.seats[i].occupied = room.slots[i].state != net::SlotState::Empty;
        view.seats[i].name = room.slots[i].name;
        view.seats[i].thumb = static_cast<MapSelectScreen::Thumb>(static_cast<uint8_t>(net_->seat_quality(static_cast<uint8_t>(i))));   // connection quality
    }
    // A guest that has not been welcomed yet has no seat: it shows itself alone until the room arrives
    if (net_->phase() == net::NetGame::Phase::Connecting || net_->phase() == net::NetGame::Phase::Failed) {
        for (auto& seat : view.seats) seat = MapSelectScreen::RoomSeat{};
    }
    view.status = net_->status_text();
    map_select_.set_room(view);
    if (!net_->is_host() && net_->phase() == net::NetGame::Phase::Room) {
        map_select_.follow_host_choice(room.map_name, room.fog);
    }
}

// START on the setup screen of a room (host only): the map file's hash goes with the Start message so that every machine checks its own copy.
void Application::net_start_from_setup(const std::string& map_path) {
    if (!net_ || !net_->is_host()) return;
    std::string filename = map_path;
    const size_t slash = filename.find_last_of("/\\");
    if (slash != std::string::npos) filename = filename.substr(slash + 1);
    uint64_t hash = 0;
    if (!net::hash_file(map_path, hash)) {
        std::cerr << "[Application] Cannot read the map file: " << map_path << std::endl;
        return;
    }
    net_->set_map(filename);
    std::random_device rd;
    if (!net_->start_match(static_cast<uint32_t>(rd()), hash)) {
        audio_mixer_.play_sfx(sim::SoundID::CantGo, 1.0f, 255);              // not enough players yet
    }
}

void Application::handle_net_events() {
    if (!net_) return;
    for (const net::NetGame::Event& ev : net_->take_events()) {
        switch (ev.type) {
            case net::NetGame::Event::Type::StartRequested:
                net_load_match();
                break;
            case net::NetGame::Event::Type::Begun:
                net_begin_match();
                break;
            case net::NetGame::Event::Type::HostLeft:
                net_notice_ = "The host left the game.";
                if (state_ == AppState::Playing && !scorecard_.is_open()) return_to_map_select();
                break;
            case net::NetGame::Event::Type::Desync:
                std::cerr << "[Application] The network match is out of sync (turn " << net_->turns_executed() << ")" << std::endl;
                break;
            default:
                break;                                        // room changes are shown by sync_room_view, the rest by the overlay and the chat log
        }
    }
}

// Start was received: load the map exactly as named, check it is the same file as the host's, initialise the simulation with the shared seed,
// roster and fog option, and report. Every machine does this on the setup screen; the match begins when everybody has reported.
void Application::net_load_match() {
    const net::StartMsg& start = net_->start_info();
    std::string path;
    for (const auto& entry : map_select_.get_maps()) {
        if (entry.filename == start.map_name) path = entry.full_path;
    }
    if (path.empty()) path = "Original-Ants/Maps/" + start.map_name;
    uint64_t hash = 0;
    local_player_id_ = net_->my_seat();
    bool ok = net::hash_file(path, hash) && hash == start.map_hash;
    if (!ok) std::cerr << "[Application] The map " << start.map_name << " here is not the host's file" << std::endl;
    ok = ok && load_match(path, start.seed, start.roster, start.fog);
    if (ok) {
        if (renderer_) renderer_->set_hud_team(local_player_id_);
        apply_team_names(start.names, start.roster);
    }
    net_->report_loaded(ok);
}

// Everybody has loaded: the match runs here from now on.
void Application::net_begin_match() {
    local_player_id_ = net_->my_seat();
    hud_.set_command_sink(net_.get());
    enter_match();
}

// The session is over (the player left, the host left, a match ended and its results were closed): back to the local setup screen.
void Application::net_end_session(const std::string& notice) {
    if (net_) net_->leave();
    hud_.set_command_sink(nullptr);
    hud_.set_roster_mask(0x0F);
    MapSelectScreen::RoomView local;
    local.status = notice;                                    // a notice stays on the setup screen until the next action
    map_select_.set_room(local);
    apply_team_names(config_.team_names, 0x0F);
}

// The waiting and out-of-sync messages of a network match (remake UI: the original has no such text). A machine that waits for the next turn
// says so after one second; a desync stops the match and says so.
void Application::render_net_overlay() {
    if (!network_active() || net_->phase() != net::NetGame::Phase::Playing) return;
    std::string text;
    ants::assets::ColorRGBA colour{255, 255, 255, 255};
    if (net_->desynced()) {
        text = "Out of sync: the match has stopped.";
        colour = ants::assets::ColorRGBA{255, 90, 90, 255};
    } else if (net_->stalled_ms() >= 1000) {
        text = "Waiting for the other players...";
        const uint8_t slow = net_->laggard();
        if (slow < 4) {
            const std::string name = sim_.get_player_name(slow);
            if (!name.empty()) text = "Waiting for " + name + "...";
        }
    }
    if (text.empty()) return;
    const int32_t w = renderer_->get_text_width(text, FontSize::Small);
    const int32_t h = renderer_->get_text_height(FontSize::Small);
    const int32_t x = 17 + (441 - w) / 2;
    const int32_t y = 26;
    renderer_->fill_rect(x - 6, y - 3, w + 12, h + 6, ants::assets::ColorRGBA{0, 0, 0, 170});
    renderer_->draw_text(text, x, y, colour, FontSize::Small);
}

void Application::render_frame() {
    renderer_->begin_frame();

    if (state_ == AppState::Loading) {
        render_loading_screen();
    } else if (state_ == AppState::QuickHelp) {
        render_quick_help_screen();
    } else if (state_ == AppState::MapSelect) {
        map_select_.render(*renderer_, assets_);
    } else if (scorecard_.is_open()) {
        scorecard_.render(*renderer_, assets_);
    } else {
        const auto& world = sim_.get_world_state();
        renderer_->set_show_hp(hud_.is_show_hp());
        renderer_->render_world(world, sim_.grid(), static_cast<int32_t>(hud_.get_selected_ant_id()),
                                hud_.get_selected_ant_ids(), false, show_tile_grid_,
                                mouse_screen_x_, mouse_screen_y_,
                                hud_.get_selected_base_team_id(),
                                tick_accumulator_);
        hud_.render(*renderer_, assets_, world, renderer_->camera());
        render_net_overlay();
    }

    // Frame rate counter and frametime sparkline in the bottom right hand corner
    int fps_val = std::max(1, static_cast<int>(std::round(fps_display_value_)));
    std::string fps_text = std::to_string(fps_val) + " FPS";
    int32_t text_w = renderer_->get_text_width(fps_text, FontSize::Small);
    int32_t text_h = renderer_->get_text_height(FontSize::Small);
    int32_t text_x = 632 - text_w;
    constexpr int32_t spark_w = static_cast<int32_t>(SPARKLINE_SAMPLES);
    constexpr int32_t spark_h = 11;
    int32_t spark_x = text_x - spark_w - 6;
    int32_t spark_y = 465;
    int32_t text_y = spark_y + (spark_h - text_h) / 2;
    renderer_->draw_text(fps_text, text_x, text_y, {255, 255, 255, 255}, FontSize::Small);

    // Version number display (bottom-right next to FPS sparkline)
    std::string ver_text(ants::VERSION_STRING);
    int32_t ver_w = renderer_->get_text_width(ver_text, FontSize::Small);
    int32_t ver_x = spark_x - ver_w - 6;
    int32_t ver_y = text_y;
    renderer_->draw_text(ver_text, ver_x, ver_y, {180, 190, 200, 220}, FontSize::Small);

    // Dark translucent background plate + subtle border
    renderer_->fill_rect(spark_x - 1, spark_y - 1, spark_w + 2, spark_h + 2, ants::assets::ColorRGBA{0, 0, 0, 160});
    renderer_->draw_rect(spark_x - 1, spark_y - 1, spark_w + 2, spark_h + 2, ants::assets::ColorRGBA{80, 85, 90, 180});

    // 60 FPS reference guide line (16.67ms -> 5 pixels from bottom)
    int32_t ref_line_y = spark_y + spark_h - 5;
    renderer_->fill_rect(spark_x, ref_line_y, spark_w, 1, ants::assets::ColorRGBA{50, 80, 70, 150});

    // Render each frame history sample (oldest on left, newest on right)
    for (int32_t i = 0; i < spark_w; ++i) {
        size_t sample_idx = (frametime_index_ + static_cast<size_t>(i)) % SPARKLINE_SAMPLES;
        float ft = frametime_history_[sample_idx];

        // 33.3ms (30 FPS) is full box height (11px); 16.67ms (60 FPS) is ~5.5px
        int32_t bar_h = static_cast<int32_t>(std::round((ft / 33.333f) * static_cast<float>(spark_h)));
        bar_h = std::clamp(bar_h, 1, spark_h);
        int32_t bar_y = spark_y + spark_h - bar_h;

        // Color coding based on frame pacing:
        // Green: <= 18.0ms (55-60+ FPS, solid)
        // Yellow: 18.1 - 25.0ms (40-55 FPS, minor drop)
        // Red: > 25.0ms (< 40 FPS, stutter spike)
        ants::assets::ColorRGBA bar_color;
        if (ft <= 18.0f) {
            bar_color = ants::assets::ColorRGBA{60, 230, 90, 220};
        } else if (ft <= 25.0f) {
            bar_color = ants::assets::ColorRGBA{240, 210, 50, 220};
        } else {
            bar_color = ants::assets::ColorRGBA{240, 60, 60, 240};
        }

        renderer_->fill_rect(spark_x + i, bar_y, 1, bar_h, bar_color);
    }

    // Authentic Software Cursor (Matching Ants.exe 0x1026c5c / 0x1027e65)
    CursorType cur = CursorType::Normal;
    if (state_ == AppState::Playing && !scorecard_.is_open()) {
        cur = hud_.evaluate_cursor(mouse_screen_x_, mouse_screen_y_, sim_.get_world_state(), sim_.grid(), renderer_->camera());
    }
    renderer_->render_software_cursor(cur, mouse_screen_x_, mouse_screen_y_, static_cast<uint32_t>(sim_.current_tick()));

    renderer_->end_frame();
}

void Application::toggle_fullscreen() {
    if (!window_) return;
    config_.fullscreen = !config_.fullscreen;
    uint32_t flags = config_.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0;
    SDL_SetWindowFullscreen(window_, flags);
    if (renderer_) {
        renderer_->set_fullscreen(config_.fullscreen);
    }
}

void Application::play_next_ingame_music() {
    static const std::string IN_GAME_TRACKS[3] = {
        "ANTS2A",
        "ANTS2B",
        "ANTSFUN3"
    };

    // Authentic shuffle sequence: rand() % 3, avoiding immediate repeat of previous track
    int track = std::rand() % 3;
    if (track == last_music_track_) {
        track = (track + 1) % 3;
    }
    last_music_track_ = track;

    audio_mixer_.play_music("Original-Ants/" + IN_GAME_TRACKS[track] + ".mp3", false);
    midi_player_.load_file("Original-Ants/" + IN_GAME_TRACKS[track] + ".MID");
    if (config_.headless) {
        midi_player_.play(false);
    }
}

void Application::render_loading_screen() {
    // 1. Fill entire 640x480 canvas with authentic solid orange #DB4B13
    renderer_->fill_rect(0, 0, 640, 480, ants::assets::ColorRGBA{219, 75, 19, 255});

    // 2. Draw outer border frame tiles from antslogo sequence (excluding dclay tiles and content bitmaps)
    const auto* seq = assets_.find_animation("antslogo");
    if (seq && !seq->subitems.empty()) {
        for (const auto& fr : seq->subitems[0].frames) {
            // Exclude dclay48 (0), dclay96 (2), strip (160), credits (161), logo (162)
            if (fr.sprite_index != 0 && fr.sprite_index != 2 &&
                fr.sprite_index != 160 && fr.sprite_index != 161 && fr.sprite_index != 162) {
                renderer_->draw_sprite(fr.sprite_index, fr.dx, fr.dy);
            }
        }
    }

    // 3. Draw authentic logo.bmp at (25, 23)
    renderer_->draw_named_sprite("logo.bmp", 25, 23);

    // 4. Draw credits.bmp at (32, 299)
    renderer_->draw_named_sprite("credits.bmp", 32, 299);

    // 5. Draw strip.bmp at (40, 315) on top of credits to authentically mask the subtitle line
    renderer_->draw_named_sprite("strip.bmp", 40, 315);

    // 6. Loading progress bar inside designated indicator slot at x=229, y=448, w=234, h=8 in authentic #1F1733
    int32_t fill_w = std::min(234, static_cast<int32_t>((intro_ticks_ * 234) / 25));
    if (fill_w > 0) {
        renderer_->fill_rect(229, 448, fill_w, 8, ants::assets::ColorRGBA{31, 23, 51, 255});
    }
}

void Application::render_quick_help_screen() {
    // qh_screen composite (last part first) and the START button animations qh_start1 / qh_start2 (hover) / qh_start3
    // (pressed) with absolute coordinates
    draw_animation_frame0(*renderer_, assets_, "qh_screen");
    draw_animation_frame0(*renderer_, assets_, quick_help_start_pressed_ ? "qh_start3" : (quick_help_start_hovered_ ? "qh_start2" : "qh_start1"));
}

void Application::play_startup_sound() {
    // Authentic game start sound: picks one of 6 random voice clips (Sound IDs 7..12)
    // rndm6.wav (7), rndm5.wav (8), rndm4.wav (9), rndm3.wav (10), rndm2.wav (11), rndm1.wav (12)
    // Includes "We love this game!", "Spike and I are buds", etc.
    uint32_t s_id = 7 + static_cast<uint32_t>(std::rand() % 6);
    audio_mixer_.play_sfx(s_id, 1.0f, 255);
}

void Application::set_local_player(uint8_t team_id) {
    if (team_id >= 4) return;
    local_player_id_ = team_id;
    sim_.set_viewing_player_id(local_player_id_);
    map_select_.set_player_team(local_player_id_);
    hud_.init(local_player_id_);
    hud_.clear_selection();
    if (renderer_) {
        renderer_->set_hud_team(local_player_id_);
        const auto* base = sim_.grid().find_anthill(local_player_id_);
        if (base) {
            renderer_->camera().center_on(base->x * TILE_SIZE + 2 * TILE_SIZE,
                                          base->y * TILE_SIZE + 2 * TILE_SIZE,
                                          current_level_.width, current_level_.height);
        }
    }
}

} // namespace ants::app
