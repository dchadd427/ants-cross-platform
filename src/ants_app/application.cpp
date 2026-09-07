#include "ants_app/application.hpp"
#include <iostream>
#include <cstring>
#include <algorithm>
#include <cstdlib>
#if defined(_WIN32)
  #include <winsock2.h>
  #include <windows.h>
#else
  #include <unistd.h>
#endif

namespace ants::app {

namespace {

std::string get_system_username() {
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
}

} // anonymous namespace

Application::Application() = default;

Application::~Application() {
    shutdown();
}

bool Application::init(int argc, char* argv[]) {
    ApplicationConfig cfg{};

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--headless") == 0) {
            cfg.headless = true;
            cfg.start_in_map_select = false;
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
        }
    }
    return init(cfg);
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

    SDL_SetHint(SDL_HINT_GRAB_KEYBOARD, "1");

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
        SDL_SetWindowKeyboardGrab(window_, SDL_TRUE);
    }

    // 7. Initialize Renderer
    renderer_ = std::make_unique<Renderer>();
    if (!renderer_->init(window_, assets_, config_.integer_scaling)) {
        std::cerr << "[Application] Failed to initialize Renderer" << std::endl;
        return false;
    }

    renderer_->set_level(current_level_);

    // Center camera on Player 0's base spawn
    const auto* base = sim_.grid().find_anthill(0);
    if (base) {
        renderer_->camera().center_on(base->x * TILE_SIZE, base->y * TILE_SIZE,
                                      current_level_.width, current_level_.height);
    } else if (!current_level_.anthill_spawns.empty()) {
        const auto& spawn = current_level_.anthill_spawns[0];
        renderer_->camera().center_on(spawn.x * TILE_SIZE, spawn.y * TILE_SIZE,
                                      current_level_.width, current_level_.height);
    }

    // 8. Initialize HUD and Scorecard
    hud_.init(0);
    local_player_id_ = 0;

    scorecard_.set_on_replay([this]() {
        scorecard_.hide();
        sim_.init(current_level_, config_.random_seed + 1);
        hud_.reset();
        midi_player_.stop(); // In-game music stays silent
    });

    scorecard_.set_on_quit([this]() {
        quit();
    });

    hud_.set_on_quit([this]() {
        quit();
    });

    hud_.set_on_sfx_volume([this](float v) {
        audio_mixer_.set_sfx_volume(v);
    });

    hud_.set_on_music_volume([this](float v) {
        midi_player_.set_volume(v);
    });

    hud_.set_on_scroll_rate([this](float r) {
        if (renderer_) {
            renderer_->camera().scroll_speed = 240.0f + r * 480.0f;
        }
    });

    // 9. Initialize Map Selection Screen
    std::string player_name = get_system_username();
    map_select_.init("Original-Ants/Maps");
    map_select_.set_player_name(player_name);
    scorecard_.set_local_player_name(player_name);
    map_select_.set_on_start([this](const std::string& map_path) {
        start_game(map_path);
    });
    map_select_.set_on_quit([this]() {
        quit();
    });

    // Determine initial AppState & MIDI lifecycle
    if (!config_.start_in_map_select) {
        state_ = AppState::Playing;
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
    } else {
        state_ = AppState::MapSelect;
        midi_player_.play(true); // Loop INTRO.MID exclusively during map selection
    }

    is_running_ = true;
    last_tick_time_ = SDL_GetPerformanceCounter();
    mouse_screen_x_ = 320;
    mouse_screen_y_ = 240;
    mouse_has_moved_ = false;
    return true;
}

void Application::shutdown() {
    is_running_ = false;

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
    // 1. Stop INTRO.MID immediately upon entering match gameplay
    midi_player_.stop();

    // 2. Load the chosen map level
    if (!current_level_.load_from_file(map_path)) {
        std::cerr << "[Application] Failed to load level: " << map_path << std::endl;
        return false;
    }

    config_.default_map_path = map_path;

    // 3. Re-initialize simulation
    sim_.init(current_level_, config_.random_seed);

    // 4. Update renderer & camera
    if (renderer_) {
        renderer_->set_level(current_level_);
        if (!current_level_.anthill_spawns.empty()) {
            const auto& spawn = current_level_.anthill_spawns[0];
            renderer_->camera().center_on(spawn.x * TILE_SIZE, spawn.y * TILE_SIZE,
                                          current_level_.width, current_level_.height);
        }
    }

    // 5. Reset HUD & Scorecard
    hud_.init(0);
    hud_.reset();
    scorecard_.hide();

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

    // 6. Transition to Playing state
    state_ = AppState::Playing;
    return true;
}

void Application::return_to_map_select() {
    state_ = AppState::MapSelect;
    scorecard_.hide();
    hud_.close_quit_dialog();
    hud_.close_quick_help();
    hud_.close_options();
    mouse_screen_x_ = 320;
    mouse_screen_y_ = 240;
    mouse_has_moved_ = false;
    midi_player_.play(true); // Resumes INTRO.MID during map selection
}

int Application::run() {
    uint64_t perf_freq = SDL_GetPerformanceFrequency();
    uint64_t last_frame_time = SDL_GetPerformanceCounter();

    int headless_frame_count = 0;

    while (is_running_) {
        uint64_t current_time = SDL_GetPerformanceCounter();
        float delta_time = static_cast<float>(current_time - last_frame_time) / static_cast<float>(perf_freq);
        last_frame_time = current_time;

        if (delta_time > 0.0001f) {
            float instant_fps = 1.0f / delta_time;
            current_fps_ = current_fps_ * 0.9f + instant_fps * 0.1f;
        }

        handle_events();

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
            if (++headless_frame_count >= 10) {
                is_running_ = false;
            }
        }

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
}

void Application::handle_events() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            quit();
            return;
        }

        if (state_ == AppState::MapSelect) {
            switch (event.type) {
                case SDL_KEYDOWN:
                    map_select_.handle_key_down(event.key.keysym.sym);
                    break;
                case SDL_MOUSEMOTION:
                    map_select_.handle_mouse_motion(event.motion.x, event.motion.y);
                    break;
                case SDL_MOUSEBUTTONDOWN:
                    map_select_.handle_mouse_down(event.button.x, event.button.y, event.button.button);
                    break;
                case SDL_MOUSEBUTTONUP:
                    map_select_.handle_mouse_up(event.button.x, event.button.y, event.button.button);
                    break;
                default:
                    break;
            }
            continue;
        }

        // Gameplay Event Dispatch
        switch (event.type) {
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

    if (state_ == AppState::MapSelect) {
        return;
    }

    handle_camera_panning(0.020f);
}

void Application::handle_camera_panning(float dt) {
    if (state_ == AppState::MapSelect || scorecard_.is_open()) return;

    float pan_x = 0.0f, pan_y = 0.0f;

    // Keyboard Arrow Keys / WASD
    const uint8_t* keystate = SDL_GetKeyboardState(nullptr);
    if (keystate) {
        if (keystate[SDL_SCANCODE_UP] || keystate[SDL_SCANCODE_W]) pan_y -= 1.0f;
        if (keystate[SDL_SCANCODE_DOWN] || keystate[SDL_SCANCODE_S]) pan_y += 1.0f;
        if (keystate[SDL_SCANCODE_LEFT]) pan_x -= 1.0f;
        if (keystate[SDL_SCANCODE_RIGHT]) pan_x += 1.0f;
    }

    // Edge Pan Scrolling (Command & Conquer / League of Legends style)
    // Uses logical canvas coordinate space (640x480)
    constexpr int LOGICAL_W = 640;
    constexpr int LOGICAL_H = 480;
    constexpr int EDGE_MARGIN = 24; // 24px border zone

    if (mouse_has_moved_ &&
        mouse_screen_x_ >= 0 && mouse_screen_x_ < LOGICAL_W &&
        mouse_screen_y_ >= 0 && mouse_screen_y_ < LOGICAL_H) {
        if (mouse_screen_x_ <= EDGE_MARGIN) {
            pan_x -= 1.0f;
        } else if (mouse_screen_x_ >= LOGICAL_W - EDGE_MARGIN) {
            pan_x += 1.0f;
        }
        if (mouse_screen_y_ <= EDGE_MARGIN) {
            pan_y -= 1.0f;
        } else if (mouse_screen_y_ >= LOGICAL_H - EDGE_MARGIN) {
            pan_y += 1.0f;
        }
    }

    if (pan_x != 0.0f || pan_y != 0.0f) {
        if (renderer_) {
            renderer_->camera().pan(pan_x, pan_y, dt, current_level_.width, current_level_.height);
        }
    }
}

void Application::handle_key_down(const SDL_KeyboardEvent& key) {
    if (key.keysym.sym == SDLK_ESCAPE) {
        if (scorecard_.is_open()) {
            return_to_map_select();
        } else if (hud_.get_active_order_mode() != sim::OrderType::None) {
            hud_.cancel_order_mode();
        } else {
            hud_.clear_selection();
        }
        return;
    }

    if (scorecard_.is_open()) return;

    // L / Ctrl+L: Toggle Unit Health Display
    if (key.keysym.sym == SDLK_l) {
        show_unit_health_ = !show_unit_health_;
        hud_.queue_news_message(show_unit_health_ ? "Unit Health Display: ON" : "Unit Health Display: OFF", 60, false);
        return;
    }

    // Tile Grid Display Toggle: T, Ctrl+T, Cmd+T, Option+T, F3, or F10
    if (key.keysym.sym == SDLK_t || key.keysym.sym == SDLK_F3 || key.keysym.sym == SDLK_F10) {
        show_tile_grid_ = !show_tile_grid_;
        hud_.queue_news_message(show_tile_grid_ ? "Tile Grid: ON" : "Tile Grid: OFF", 60, false);
        return;
    }

    if (key.keysym.sym == SDLK_F12) {
        renderer_->save_screenshot("screenshot.png");
        hud_.queue_news_message("Screenshot saved to screenshot.png", 60, false);
        return;
    }

    if (key.keysym.sym == SDLK_h) {
        bool ctrl = (key.keysym.mod & KMOD_CTRL) || (key.keysym.mod & KMOD_GUI);
        if (ctrl) {
            if (hud_.get_selected_base_team_id() == local_player_id_) {
                sim_.hatch_ant(local_player_id_, sim::AntType::Worker);
            } else {
                hud_.select_base(local_player_id_);
                hud_.queue_news_message("Home Anthill Selected", 40, false);
            }
            return;
        }
    }

    if (key.keysym.sym == SDLK_a) {
        hud_.select_all_friendly(sim_.get_world_state());
        hud_.queue_news_message("All Friendly Ants Selected", 40, false);
        return;
    }

    if (key.keysym.sym == SDLK_m) {
        if (midi_player_.is_playing()) {
            midi_player_.stop();
            hud_.queue_news_message("Music Muted", 40, false);
        } else {
            midi_player_.play(true);
            hud_.queue_news_message("Music Enabled", 40, false);
        }
        return;
    }

    if (key.keysym.sym == SDLK_SPACE) {
        if (hud_.get_selected_ant_id() != 0) {
            const auto& world = sim_.get_world_state();
            for (const auto& a : world.ants) {
                if (a.id == hud_.get_selected_ant_id()) {
                    renderer_->camera().center_on(a.px, a.py, current_level_.width, current_level_.height);
                    return;
                }
            }
        }
        const auto* base = sim_.grid().find_anthill(local_player_id_);
        if (base) {
            renderer_->camera().center_on(base->x * 32, base->y * 32, current_level_.width, current_level_.height);
        }
        return;
    }

    hud_.handle_key_down(key.keysym.sym, sim_, renderer_->camera());
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

    if (button.type == SDL_MOUSEBUTTONDOWN) {
        hud_.handle_mouse_down(button.x, button.y, button.button, sim_, renderer_->camera());
    } else if (button.type == SDL_MOUSEBUTTONUP) {
        hud_.handle_mouse_up(button.x, button.y, button.button, sim_, renderer_->camera());
    }
}

void Application::update_simulation(float dt) {
    if (state_ == AppState::MapSelect) {
        midi_player_.update(dt);
        return;
    }

    tick_accumulator_ += dt;
    while (tick_accumulator_ >= 0.050f) {
        if (!sim_.is_match_over()) {
            sim_.tick();

            const auto& world = sim_.get_world_state();
            hud_.update(world, 1);
            hud_.poll_sim_events(sim_);

            auto audio_events = sim_.poll_audio_events();
            audio_mixer_.ingest_simulation_events(audio_events, local_player_id_);

            if (sim_.is_match_over()) {
                scorecard_.show(world.match_result, local_player_id_);
                uint32_t sting_sound = scorecard_.get_audio_to_play();
                if (sting_sound > 0) {
                    audio_mixer_.play_sfx(sting_sound, 1.0f, 255);
                    scorecard_.clear_audio_to_play();
                }
                midi_player_.fade_out(1.0f);
            }
        }
        tick_accumulator_ -= 0.050f;
    }

    // Update spatial audio listener position
    audio_mixer_.set_listener_position(renderer_->camera().world_x + PLAYFIELD_W / 2,
                                       renderer_->camera().world_y + PLAYFIELD_H / 2);

    midi_player_.update(dt);
}

void Application::render_frame() {
    renderer_->begin_frame();

    if (state_ == AppState::MapSelect) {
        map_select_.render(*renderer_, assets_);
    } else if (scorecard_.is_open()) {
        scorecard_.render(*renderer_, assets_);
    } else {
        const auto& world = sim_.get_world_state();
        renderer_->render_world(world, sim_.grid(), static_cast<int32_t>(hud_.get_selected_ant_id()),
                                hud_.get_selected_ant_ids(), show_unit_health_, show_tile_grid_,
                                mouse_screen_x_, mouse_screen_y_,
                                hud_.get_selected_base_team_id());
        hud_.render(*renderer_, assets_, world, renderer_->camera());
    }

    // Frame rate counter in the bottom right hand corner: small white text
    int fps_val = std::max(1, static_cast<int>(std::round(current_fps_)));
    std::string fps_text = std::to_string(fps_val) + " FPS";
    int32_t text_w = static_cast<int32_t>(fps_text.size()) * 6;
    renderer_->draw_text(fps_text, 638 - text_w, 471, {255, 255, 255, 255});

    renderer_->end_frame();
}

} // namespace ants::app
