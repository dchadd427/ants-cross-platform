#include "ants_app/application.hpp"
#include "ants_app/pointer_clamp.hpp"
#include "ants_app/fps_overlay.hpp"
#include "ants_app/latency_corner.hpp"
#include "ants_app/net_overlay.hpp"
#include "ants_app/edge_scroll.hpp"
#include "ants_app/ui_anim.hpp"
#include "ants_app/version.hpp"
#include "ants_ai/bot.hpp"
#include "ants_ai/bot_controller.hpp"
#include <iostream>
#include <fstream>
#include <ctime>
#include <cstring>
#include <algorithm>
#include <cstdlib>
#include <random>
#if defined(_WIN32)
  #include <winsock2.h>
  #include <windows.h>
#elif defined(__EMSCRIPTEN__)
  #include <emscripten.h>
  #include <emscripten/html5.h>                                  // the page's visibility (a hidden page runs no frames, see Application::set_page_hidden)
  #include <unistd.h>
#else
  #include <unistd.h>
#endif
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
  #if defined(__has_include) && __has_include(<SDL_syswm.h>)
    #include <SDL_syswm.h>
  #else
    #include <SDL2/SDL_syswm.h>
  #endif
  #include <objc/message.h>                                  // os_fullscreen asks the window's NSWindow for its style mask (libobjc, src/ants_app/CMakeLists.txt)
  #include <objc/runtime.h>
#endif

#if defined(__EMSCRIPTEN__)
// Tells the page that embeds this game (web/four.html) the tick and the state hash (high word first, 16 hex digits). A game in a frame posts to its parent; a game in
// a window of its own posts on the broadcast channel "ants-sync" of its origin, with its room code, for the page that opened it or any page of the site that listens.
// EM_JS and not EM_ASM: the `$0` of EM_ASM is a warning under -Wpedantic.
extern "C" {
EM_JS(void, ants_post_sync_to_parent, (int seat, int tick, int hash_high, int hash_low), {
    var hash = (hash_high >>> 0).toString(16).padStart(8, '0') + (hash_low >>> 0).toString(16).padStart(8, '0');
    if (window.parent !== window) {
        window.parent.postMessage({ants: 'sync', seat: seat, tick: tick, hash: hash}, location.origin);
    } else if (typeof BroadcastChannel !== "undefined") {
        try {
            if (!window.antsSyncChannel) window.antsSyncChannel = new BroadcastChannel("ants-sync");
            window.antsSyncChannel.postMessage({ants: "sync", seat: seat, tick: tick, hash: hash, room: new URLSearchParams(location.search).get("room") || ""});
        } catch (e) {}
    }
});
}

// The one application of the page (main() keeps it alive for as long as the page lives): the entry for ants_background_pump below
namespace {
ants::app::Application* g_web_app = nullptr;
}  // namespace
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

// 0x100e714: the Music Volume v (0 .. 100) becomes the device's volume word v * 0xffff / 100 (integer division); as a fraction of the full scale
float music_volume_of(int32_t v) {
    const int32_t word = (std::clamp(v, 0, 100) * 0xffff) / 100;
    return static_cast<float>(word) / 65535.0f;
}

// Where the commands of a bot of a local game go: straight into the simulation, as a click of the local player does
class LocalBotSink final : public sim::CommandSink {
public:
    explicit LocalBotSink(sim::SimulationEngine& sim) : sim_(sim) {}
    sim::CommandResult submit(const sim::Command& command) override { return sim_.apply_command(command); }

private:
    sim::SimulationEngine& sim_;
};

// ... and of a room's host: into the sequencer of the session, for the seat of the bot (the verdict arrives with the turn, like every command's: a bot ignores it)
class NetBotSink final : public sim::CommandSink {
public:
    NetBotSink(net::NetGame& net, uint8_t seat) : net_(net), seat_(seat) {}
    sim::CommandResult submit(const sim::Command& command) override {
        sim::CommandResult result;
        if (net_.submit_bot(seat_, command)) result.status = sim::CommandResult::Status::Applied;
        return result;
    }

private:
    net::NetGame& net_;
    uint8_t seat_;
};

constexpr uint8_t seat_bit(uint8_t seat) noexcept { return static_cast<uint8_t>(1u << seat); }

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

#if !defined(__EMSCRIPTEN__)
// The operating system holds the window fullscreen without SDL's fullscreen flags. macOS: a fullscreen Space (the green button, or Cmd+Ctrl+F, the item
// Toggle Full Screen of SDL's Window menu) carries neither SDL_WINDOW_FULLSCREEN nor SDL_WINDOW_FULLSCREEN_DESKTOP, only SDL_WINDOW_MAXIMIZED, which a plain
// zoom gives too; the NSWindow's style mask tells (NSWindowStyleMaskFullScreen, 1 << 14). Elsewhere false: a fullscreen toggle of a Linux window manager
// (F11, a menu) is NOT detected (SDL does not report it and the game does not ask X11 or Wayland), so such a window does not grab the pointer; Windows
// has no such toggle for a normal window.
bool os_fullscreen(SDL_Window* window) {
#if defined(__APPLE__) && defined(SDL_VIDEO_DRIVER_COCOA)
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (window == nullptr || SDL_GetWindowWMInfo(window, &info) != SDL_TRUE || info.subsystem != SDL_SYSWM_COCOA || info.info.cocoa.window == nullptr) return false;
    using StyleMaskFn = unsigned long (*)(id, SEL);                     // -[NSWindow styleMask] returns an NSUInteger
    const unsigned long mask = reinterpret_cast<StyleMaskFn>(objc_msgSend)(reinterpret_cast<id>(info.info.cocoa.window), sel_registerName("styleMask"));
    return (mask & (1ul << 14)) != 0;
#else
    (void)window;
    return false;
#endif
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

ApplicationConfig Application::parse_arguments(int argc, char* argv[]) {
    ApplicationConfig cfg{};
    cfg.aspect = kPlatformDefaultAspect;                                      // 16:9 on a desktop; --aspect and the settings' key win. (A config that is made by hand, as the tests do, is the original's 4:3.)
#if defined(__EMSCRIPTEN__)
    cfg.label_unnamed_teams = false;                                          // no other players in the browser build: no placeholder labels
#endif

    // The desktop start menu (start_menu.hpp) is shown by a native game that is started without a mode: every option that starts something directly (a match, a room, a test run, a
    // screenshot) says so here and skips the menu; the options that only set something up (--name, --settings, --seed, --team-name, --title, the window options ...) do not
    bool mode_given = false;
    bool menu_forced = false;
    bool direct_match = false;                                                 // --map, --open-options, --scorecard: a match starts at once (the menu cannot come first)
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--headless") == 0) {
            cfg.headless = true;
            mode_given = true;
        } else if (std::strcmp(argv[i], "--map") == 0 && i + 1 < argc) {
            cfg.default_map_path = argv[++i];
            cfg.start_in_map_select = false;
            mode_given = direct_match = true;
        } else if (std::strcmp(argv[i], "--settings") == 0 && i + 1 < argc) {
            cfg.settings_path = argv[++i];
        } else if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            cfg.random_seed = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (std::strcmp(argv[i], "--fullscreen") == 0) {
            cfg.fullscreen = true;
        } else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            cfg.screenshot_path = argv[++i];
            mode_given = true;
        } else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            cfg.screenshot_frames = std::stoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--select-ant") == 0 && i + 1 < argc) {
            cfg.select_ant_id = std::stoi(argv[++i]);
            mode_given = true;
        } else if (std::strcmp(argv[i], "--select-base") == 0 && i + 1 < argc) {
            cfg.select_base_team = std::stoi(argv[++i]);
            mode_given = true;
        } else if (std::strcmp(argv[i], "--open-options") == 0) {
            cfg.open_options = true;
            cfg.start_in_map_select = false;
            mode_given = direct_match = true;
        } else if (std::strcmp(argv[i], "--show-grid") == 0) {
            cfg.show_tile_grid = true;
        } else if (std::strcmp(argv[i], "--map-select") == 0) {
            cfg.start_in_map_select = true;
            mode_given = true;                                                 // "start on the setup screen": today's start, without the menu
        } else if (std::strcmp(argv[i], "--player") == 0 && i + 1 < argc) {
            cfg.local_player_id = static_cast<uint8_t>(std::stoi(argv[++i]));
            mode_given = true;
        } else if (std::strcmp(argv[i], "--scorecard") == 0) {
            cfg.show_scorecard = true;
            cfg.start_in_map_select = false;
            mode_given = direct_match = true;
        } else if (std::strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
            cfg.player_name = argv[++i];                                       // this player's name: the room, the HUD, chat, the results
        } else if (std::strcmp(argv[i], "--team-name") == 0 && i + 2 < argc) {   // --team-name <0-3> <name>, for a local game
            const int team = std::atoi(argv[i + 1]);
            if (team >= 0 && team < 4) cfg.team_names[static_cast<size_t>(team)] = argv[i + 2];
            i += 2;
        } else if (argv[i][0] == '-' && argv[i][1] == 'N' && argv[i][2] >= '0' && argv[i][2] <= '3') {
            cfg.team_names[static_cast<size_t>(argv[i][2] - '0')] = argv[i] + 3;   // the original's -N<team><name>
        } else if (std::strncmp(argv[i], "-pnum=", 6) == 0 || std::strncmp(argv[i], "-pnum:", 6) == 0) {
            const int team = std::atoi(argv[i] + 6);                           // the original's local team (its own spelling has the colon, 0x1047134)
            if (team >= 0 && team < 4) cfg.local_player_id = static_cast<uint8_t>(team);
            mode_given = true;
        } else if (std::strcmp(argv[i], "--host") == 0) {
            cfg.net_role = ApplicationConfig::NetRole::Host;
            mode_given = true;
            if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9') {   // an optional port
                cfg.net_port = static_cast<uint16_t>(std::stoul(argv[++i]));
            }
        } else if (std::strcmp(argv[i], "--join") == 0 && i + 1 < argc) {
            cfg.net_role = ApplicationConfig::NetRole::Join;
            mode_given = true;
            std::string target = argv[++i];                                    // host or host:port
            const size_t colon = target.rfind(':');
            if (colon != std::string::npos && target.find(':') == colon) {
                cfg.net_port = static_cast<uint16_t>(std::stoul(target.substr(colon + 1)));
                target.resize(colon);
            }
            cfg.net_address = target;
        } else if (std::strcmp(argv[i], "--join-url") == 0 && i + 1 < argc) {
            cfg.net_role = ApplicationConfig::NetRole::Join;
            mode_given = true;
            cfg.net_url = argv[++i];                                           // ws:// or wss://: through a game server's WebSocket door
        } else if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            cfg.net_port = static_cast<uint16_t>(std::stoul(argv[++i]));
        } else if (std::strcmp(argv[i], "--loopback") == 0) {
            cfg.net_loopback_only = true;
        } else if (std::strcmp(argv[i], "--lan-port") == 0 && i + 1 < argc) {
            cfg.lan_port = static_cast<uint16_t>(std::stoul(argv[++i]));
        } else if (std::strcmp(argv[i], "--no-lan") == 0) {
            cfg.lan_port = 0;                                                  // the room is not announced on the local network
        } else if (std::strcmp(argv[i], "--room") == 0 && i + 1 < argc) {
            const std::string code = argv[++i];
            if (net::valid_room_code(code)) cfg.net_room = code;               // letters, digits, '_' and '-' (up to 32); anything else is ignored
            mode_given = true;
        } else if (std::strcmp(argv[i], "--token") == 0 && i + 1 < argc) {
            cfg.net_token = argv[++i];
            mode_given = true;
        } else if (std::strcmp(argv[i], "--seat") == 0 && i + 1 < argc) {
            const int seat = std::atoi(argv[++i]);                             // the colour to sit in: 0 green, 1 red, 2 blue, 3 black
            if (seat >= 0 && seat < 4) cfg.net_seat = static_cast<uint8_t>(seat);
            mode_given = true;
        } else if (std::strcmp(argv[i], "--start-when") == 0 && i + 1 < argc) {
            const int players = std::atoi(argv[++i]);                          // a test hook: the leader of a server's room presses START once this many players are in
            if (players >= 2 && players <= 4) cfg.net_start_when = static_cast<uint8_t>(players);
        } else if (std::strcmp(argv[i], "--title") == 0 && i + 1 < argc) {
            cfg.title = argv[++i];
        } else if (std::strcmp(argv[i], "--window-pos") == 0 && i + 1 < argc) {
            cfg.has_window_pos = parse_pair(argv[++i], cfg.window_x, cfg.window_y);
        } else if (std::strcmp(argv[i], "--window-size") == 0) {                    // --window-size WxH or W,H (at least 320x240); anything else is refused, as --aspect's is
            int32_t w = 0;
            int32_t h = 0;
            std::string why;
            if (i + 1 >= argc) {
                if (cfg.startup_error.empty()) cfg.startup_error = "--window-size needs WIDTHxHEIGHT or WIDTH,HEIGHT (for example 1280x720)";
            } else if (parse_window_size(argv[++i], w, h, why)) {
                cfg.has_window_size = true;
                cfg.window_w = w;
                cfg.window_h = h;
            } else if (cfg.startup_error.empty()) {
                cfg.startup_error = "--window-size " + why;
            }
        } else if (std::strcmp(argv[i], "--grid") == 0 && i + 1 < argc) {
            if (!parse_grid(argv[++i], cfg.grid_cols, cfg.grid_rows)) cfg.grid_cols = cfg.grid_rows = 0;
        } else if (std::strcmp(argv[i], "--cell") == 0 && i + 1 < argc) {
            cfg.grid_cell = std::max(0, std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--display") == 0 && i + 1 < argc) {
            cfg.display_index = std::max(-1, std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--audio-focus") == 0) {
            cfg.audio_follows_focus = true;
        } else if (std::strcmp(argv[i], "--server") == 0) {                    // the game server of the start menu's Join and Host: HOST[:PORT]
            ServerAddress parsed;
            std::string why;
            if (i + 1 >= argc) {
                if (cfg.startup_error.empty()) cfg.startup_error = "--server needs HOST[:PORT], e.g. --server beta.playants.org:4001";
            } else {
                cfg.server = argv[++i];
                if (!parse_server(cfg.server, parsed, why) && cfg.startup_error.empty()) cfg.startup_error = "--server " + cfg.server + ": " + why;
            }
        } else if (std::strcmp(argv[i], "--start-menu") == 0) {                // forces the start menu (also headless, with --screenshot: the tests and the screenshots)
            menu_forced = true;
        } else if (std::strcmp(argv[i], "--aspect") == 0) {                      // --aspect 16:9 | 4:3 (nothing else exists yet)
            std::string why;
            if (i + 1 >= argc) {
                if (cfg.startup_error.empty()) cfg.startup_error = "--aspect needs 16:9 or 4:3";
            } else if (parse_aspect(argv[++i], cfg.aspect, why)) {
                cfg.aspect_given = true;
            } else if (cfg.startup_error.empty()) {
                cfg.startup_error = "--aspect " + why;
            }
        } else if (std::strcmp(argv[i], "--bot") == 0) {                       // a computer player: --bot SEAT[:SPEC], repeatable (docs/BOTS.md)
            mode_given = true;
            ai::BotSpec spec;
            std::string why;
            if (i + 1 >= argc) {
                if (cfg.startup_error.empty()) cfg.startup_error = "--bot needs SEAT[:SPEC], e.g. --bot 1:hard";
            } else if (ai::parse_bot_spec(argv[++i], spec, why)) {
                cfg.bots.push_back(spec);
            } else if (cfg.startup_error.empty()) {
                cfg.startup_error = "--bot " + std::string(argv[i]) + ": " + why;
            }
        }
    }
#if !defined(__EMSCRIPTEN__)
    if (menu_forced) {
        // The menu comes first and chooses the match: an option that starts a match or a room at once cannot be combined with it
        if ((direct_match || cfg.net_role != ApplicationConfig::NetRole::None) && cfg.startup_error.empty()) {
            cfg.startup_error = "--start-menu cannot be combined with --map, --open-options, --scorecard, --host, --join or --join-url: they start a match or a room at once";
        }
        cfg.start_menu = true;
    } else {
        cfg.start_menu = !mode_given;
    }
#else
    (void)mode_given;                                                          // the browser build never shows the menu (its page has its own controls)
    (void)menu_forced;
    (void)direct_match;
#endif
    return cfg;
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
    local_roster_ = local_bots ? bot_roster(own_seat) : uint8_t{0x0F};

    // 1. Initialize SDL2
    uint32_t sdl_flags = SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER;
    if (config_.headless) {
        SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
    }

    // Several games on one screen: the click that activates a window is also a click in it (the first click on a background window is not lost)
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");

    if (SDL_Init(sdl_flags) != 0) {
        std::cerr << "[Application] SDL_Init Error: " << SDL_GetError() << std::endl;
        return false;
    }

    // 2. Load Master Asset Archive (ants.chd)
    if (!assets_.load_from_file(config_.chd_path)) {
        std::cerr << "[Application] Failed to load CHD archive: " << config_.chd_path << std::endl;
        return false;
    }

    // 3. Load Map Level: the list is what the Maps folder holds; a game that starts without the setup screen plays --map, else the first map of the list
    map_select_.init(config_.maps_dir);
    if (config_.default_map_path.empty()) config_.default_map_path = map_select_.get_selected_map_path();
    if (config_.default_map_path.empty()) {
        std::cerr << "[Application] There is no map (*.lvl) in " << config_.maps_dir << std::endl;
        return false;
    }
    ants::assets::LevelValidation start_verdict;
    if (!current_level_.load_from_file(config_.default_map_path, &start_verdict)) {
        std::cerr << "[Application] Failed to load Level: " << config_.default_map_path << std::endl;
        if (start_verdict.first_fatal()) std::cerr << "[Application]   " << start_verdict.reason() << std::endl;
        return false;
    }
    // A game that starts straight into its match (--map, no setup screen, no network) plays all four teams: the level must be playable by them, as load_match demands
    // of every match that starts from the setup screen (a start marker outside the grid has no behaviour in the original)
    if (!config_.start_in_map_select && config_.net_role == ApplicationConfig::NetRole::None) {
        start_verdict = current_level_.validate(0x0F);
        if (!start_verdict.playable) {
            std::cerr << "[Application] The level cannot be played: " << config_.default_map_path << std::endl;
            std::cerr << "[Application]   " << start_verdict.reason() << std::endl;
            return false;
        }
    }

    // 4. Initialize Simulation Engine. A local game that starts at once with bots plays the seats that are taken (you and the bots): a team nobody plays has no hill
    // and no ants (the setup screen's game does the same in start_game)
    if (local_bots && !config_.start_in_map_select) {
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
    picture_ = picture_for_state();                                                          // the match screen is the whole canvas, the original's pages are its 640 x 480 picture, centred
    renderer_->set_picture(picture_);
    renderer_->set_layout(layout_);
    renderer_->set_level(current_level_);

    show_start_view();

    // 8. Initialize HUD and Scorecard
    hud_.set_text_metrics(renderer_.get());
    hud_.set_layout(layout_);
    hud_.init(0);
    hud_.options().load(config_store_);
    hud_.set_config_store(&config_store_);
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
            start_local_bots(config_.random_seed + 1);
        } else {
            sim_.init(current_level_, config_.random_seed + 1);
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
    map_select_.set_on_quit([this]() {
        leave_game();                                         // (a network game that the start menu led to: back to the menu)
    });
    map_select_.set_on_play_sfx([this](uint32_t sound_id) { play_ui_sound(sound_id); });

    // A network game: the room is the setup screen (host: pick the map and START; guest: follow the host's choice)
    if (networked) {
        config_.start_in_map_select = true;
        net_ = std::make_unique<net::NetGame>(sim_);
        net_->set_discovery(config_.lan_port);                                          // an open room announces itself to the local network (ants_net/lan.hpp)
        net_->set_game_version(std::string(VERSION_STRING));
        const bool host_role = config_.net_role == ApplicationConfig::NetRole::Host;
        const bool ok = host_role                   ? net_->host(config_.net_port, player_name, config_.net_loopback_only)
                        : !config_.net_url.empty() ? net_->join_url(config_.net_url, player_name, config_.net_seat, config_.net_room, config_.net_token)
                                                   : net_->join(config_.net_address, config_.net_port, player_name, config_.net_seat, config_.net_room, config_.net_token);
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
        if (net_->is_host() && !map_select_.get_maps().empty()) net_->set_map(map_select_.get_maps()[static_cast<size_t>(map_select_.get_selected_index())].filename);
        sync_room_view();
    } else {
        // the names of a local game (the local player's own name too); a bot is called "Bot (Medium)" unless -N / --team-name says otherwise
        apply_team_names(local_bots ? local_team_names() : config_.team_names, local_bots && !config_.start_in_map_select ? local_roster_ : uint8_t{0x0F});
    }

    // The desktop start menu: part of this run when the config asks for it and nothing starts a match or a room at once (never in the web build)
#if !defined(__EMSCRIPTEN__)
    menu_enabled_ = config_.start_menu && config_.start_in_map_select && !networked;
#endif
    if (menu_enabled_) init_start_menu();

    // --audio-focus: a window that opened behind the others never receives "focus lost": it starts silent and holds its music until it gets the focus
    if (config_.audio_follows_focus && !config_.headless && window_ != nullptr) {
        SDL_PumpEvents();
        if ((SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS) == 0) set_app_active(false);
    }

    // Determine initial AppState & audio lifecycle
    if (!config_.start_in_map_select) {
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
        if (local_bots) {                                            // --map with --bot: the game is running already, the bots join it
            hud_.set_roster_mask(local_roster_);                     // (the bots are named: every taken seat has its label and its row)
            scorecard_.set_shown_teams(local_roster_);
            start_local_bots(config_.random_seed);
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
#endif
    return true;
}

void Application::set_layout(const ScreenLayout& layout) {
    layout_ = layout;
    update_picture();
    if (renderer_) renderer_->set_layout(layout_);
    hud_.set_layout(layout_);
}

// Where the picture that is on screen sits in the canvas: a match is the layout's picture (the whole canvas of its aspect), everything else is a page of the original's own 640 x 480
// screen (the loading screen, the quick help, the setup screen and the room, the results), centred
LayoutRect Application::picture_for_state() const {
    const bool match = state_ == AppState::Playing && !scorecard_.is_open();
    if (match) return canvas().centred(layout_.width, layout_.height);
    if (state_ == AppState::MapSelect && wide_setup()) return canvas().rect();            // the setup screen of a 960 x 540 canvas is composed for it (setup_layout.hpp)
    return canvas().centred(ScreenLayout::kClassicWidth, ScreenLayout::kClassicHeight);
}

// The setup screen has a wide version for the 16:9 canvas of 960 x 540 (and for no other size: any other canvas draws the original's page centred)
bool Application::wide_setup() const {
    const CanvasLayout c = canvas();
    return SetupLayout::supports(c.width, c.height);
}

// The picture changes when the screen does (a match starts, the results open, the setup screen comes back). The pointer stays where it is on the canvas, so its coordinates, which are the
// picture's own, move with the corner; a pointer that was beside the new picture (on the clay of a page, left of x = 160) is at the picture's nearest edge pixel, which is where the
// original's one-monitor pointer is when it is pushed against an edge (pointer_clamp.hpp), and not outside the picture, where the cursor would vanish.
void Application::update_picture() {
    map_select_.set_wide_layout(wide_setup());                                              // (the screen is drawn and answers the pointer as its wide version when the canvas is 960 x 540)
    const LayoutRect want = picture_for_state();
    if (want == picture_) return;
    mouse_screen_x_ = std::clamp(mouse_screen_x_ + picture_.x - want.x, 0, want.w - 1);
    mouse_screen_y_ = std::clamp(mouse_screen_y_ + picture_.y - want.y, 0, want.h - 1);
    picture_ = want;
    if (renderer_) renderer_->set_picture(picture_);
}

// --aspect, else the settings' key `aspect`, else the default of the platform (parse_arguments puts it into the config: kPlatformDefaultAspect in canvas_layout.hpp, 16:9 on a desktop and
// in the web build, whose page passes the shape it shows as `--aspect`); a config that is made by hand keeps its own aspect (the original's 4:3 unless it says otherwise). A settings file
// never stops the game: a value that is not 16:9 or 4:3 is reported and ignored. The settings of the web build are the browser's local storage (the same key).
void Application::choose_aspect() {
    aspect_ = config_.aspect;
    if (!config_.aspect_given && config_store_.has("aspect")) {
        const std::string text = config_store_.get_string("aspect", "", 16);
        std::string why;
        Aspect from_settings = Aspect::Classic4x3;
        if (parse_aspect(text, from_settings, why)) aspect_ = from_settings;
        else std::cerr << "[Application] settings: aspect=" << why << " (ignored)" << std::endl;
    }
}

// 0x10122d4: the stamp is the C runtime's `_strdate` ("mm/dd/yy") and `_strtime` ("hh:mm:ss") joined by " @ " (the format string "%s @ %s\n\n")
std::string Application::transcript_stamp(std::time_t time) {
    char date[16] = {0};
    char clock[16] = {0};
    if (const std::tm* local = std::localtime(&time)) {
        std::strftime(date, sizeof(date), "%m/%d/%y", local);
        std::strftime(clock, sizeof(clock), "%H:%M:%S", local);
    }
    return std::string(date) + " @ " + clock;
}

bool Application::write_chat_transcript(const std::string& path) const {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << hud_.chat_transcript(transcript_stamp(std::time(nullptr)));
    return static_cast<bool>(out);
}

void Application::shutdown() {
    is_running_ = false;
    if (menu_enabled_) start_menu_.flush();
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

bool Application::start_game(const std::string& map_path) {
    stop_bots();
    uint8_t roster = 0x0F;
    if (!config_.bots.empty()) {                                      // a game with bots: re-checked at START (Fog of War is chosen on this screen)
        map_select_.set_room(MapSelectScreen::RoomView{});            // (a refusal of the last START is gone)
        const uint8_t own = local_player_id_ < 4 ? local_player_id_ : uint8_t{0};
        const std::string why = bot_setup_problem(own, map_select_.is_fog_of_war_enabled());
        if (!why.empty()) {
            show_setup_notice(why);
            return false;
        }
        roster = bot_roster(own);
        local_roster_ = roster;
    }
    if (!load_match(map_path, config_.random_seed, roster, map_select_.is_fog_of_war_enabled())) return false;
    apply_team_names(config_.bots.empty() ? config_.team_names : local_team_names(), roster);
    enter_match();
    if (!config_.bots.empty()) start_local_bots(config_.random_seed);
    return true;
}

// The level, the simulation and the renderer of a match (the map file, the seed, the teams that play and the Fog of War option are the whole
// shared state: every machine of a network match calls this with the same values).
bool Application::load_match(const std::string& map_path, uint32_t seed, uint8_t roster, bool fog) {
    ants::assets::LevelValidation verdict;
    if (!current_level_.load_from_file(map_path, &verdict)) {
        std::cerr << "[Application] Failed to load level: " << map_path << std::endl;
        if (verdict.first_fatal()) std::cerr << "[Application]   " << verdict.reason() << std::endl;
        return false;
    }
    // Is the map playable by the teams of this match? (a start marker of a team that plays must lie inside the grid: the original has no behaviour for one outside)
    verdict = current_level_.validate(roster);
    if (!verdict.playable) {
        std::cerr << "[Application] The level cannot be played: " << map_path << std::endl;
        std::cerr << "[Application]   " << verdict.reason() << std::endl;
        return false;
    }
    config_.default_map_path = map_path;
    if ((roster & 0x0Fu) != 0x0Fu) current_level_ = current_level_.for_roster(roster);      // no hill art for a team without a player

    sim_.set_fog_of_war_enabled(fog);
    sim_.set_viewing_player_id(local_player_id_);
    sim_.init(current_level_, seed, roster);

    if (renderer_) {
        renderer_->set_level(current_level_);
        show_start_view();
    }
    uint8_t labelled = roster;                                                // the teams that get a score label
    if (!network_active() && !config_.label_unnamed_teams) {
        labelled = 0;
        for (uint8_t p = 0; p < 4; ++p) {
            if (((roster >> p) & 1u) != 0 && (p == local_player_id_ || !local_team_names()[p].empty())) labelled = static_cast<uint8_t>(labelled | (1u << p));
        }
    }
    hud_.set_roster_mask(labelled);
    scorecard_.set_shown_teams(labelled);                                     // the results list the same teams as the score labels
    return true;
}

// The moment a match starts for the player: music and the start sound, the HUD, the "get ready" modal, the Playing state.
void Application::enter_match() {
    // In-Game Music: Shuffle between ANTS2A, ANTS2B, ANTSFUN3. A match that begins while the page is hidden (a background step) starts no music and no sound effect: the page
    // is not heard; the music starts when it is shown (apply_pending_music)
    if (background_stepping_) {
        pending_music_ = PendingMusic::InGame;
    } else {
        play_next_ingame_music();
        // Play authentic random game startup sound (rndm1..6.wav / Sound IDs 7..12)
        play_startup_sound();
    }

    // Reset HUD & Scorecard
    hud_.init(local_player_id_);
    hud_.reset();
    hud_.start_match_modal();
    scorecard_.hide();
    match_over_handled_ = false;
    state_ = AppState::Playing;
    update_picture();                                                       // (the match screen replaces whatever page was up)

    // Reset simulated cursor to middle of screen until actually seen moving
    mouse_screen_x_ = layout_.width / 2;
    mouse_screen_y_ = layout_.height / 2;
    mouse_has_moved_ = false;

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

    state_ = AppState::Playing;
    match_started_ = true;
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

// ------------------------------------------------------------------------------------------------
// Computer players (docs/BOTS.md): the virtual clients of ants_ai. A game without --bot never gets here and runs no bot code.
// ------------------------------------------------------------------------------------------------

// "" when the game may start with the bots of the command line, else why it may not. `own_seat` is the person's seat (a room's host sits at seat 0).
std::string Application::bot_setup_problem(uint8_t own_seat, bool fog) const {
    if (config_.bots.empty()) return std::string();
    if (config_.net_role == ApplicationConfig::NetRole::Join) return "--bot cannot be used with --join: a guest never runs bots, the host's machine does.";
    ai::SetupInfo info;
    info.bots = config_.bots;
    info.fog = fog;
    info.human_mask = seat_bit(own_seat);
    info.roster = bot_roster(own_seat);
    return ai::check_setup(info);
}

uint8_t Application::bot_roster(uint8_t own_seat) const {
    uint8_t roster = seat_bit(own_seat);
    for (const ai::BotSpec& b : config_.bots) {
        if (b.seat < 4) roster = static_cast<uint8_t>(roster | seat_bit(b.seat));
    }
    return roster;
}

std::array<std::string, 4> Application::local_team_names() const {
    std::array<std::string, 4> names = config_.team_names;
    for (const ai::BotSpec& b : config_.bots) {
        if (b.seat < 4 && names[b.seat].empty()) names[b.seat] = ai::bot_display_name(b);      // a seat is shown as a bot; an explicit name wins
    }
    return names;
}

void Application::show_setup_notice(const std::string& text) {
    std::cerr << "[Application] " << text << std::endl;
    MapSelectScreen::RoomView view;                                   // the local setup screen: its prompt line is the status
    view.status = text;
    map_select_.set_room(view);
}

// The bots of a local game, built after the simulation was initialised: their commands go straight into the simulation, like the local player's clicks
bool Application::start_local_bots(uint32_t match_seed) {
    stop_bots();
    if (config_.bots.empty() || network_active()) return false;
    bots_ = std::make_unique<ai::BotController>(sim_, match_seed);
    bool all = true;
    for (const ai::BotSpec& spec : config_.bots) {
        bot_sinks_.push_back(std::make_unique<LocalBotSink>(sim_));
        std::string why;
        if (!add_bot(spec, *bot_sinks_.back(), why)) {
            std::cerr << "[Application] No bot at seat " << static_cast<unsigned>(spec.seat) << ": " << why << std::endl;
            all = false;
        }
    }
    return all;
}

// The host of a room runs the room's bots: their commands go into the host's sequencer. A guest (and a guest that took over as host) never does.
void Application::start_net_bots() {
    stop_bots();
    if (config_.bots.empty() || !net_ || !net_->is_host()) return;
    const net::RoomMsg& room = net_->room();
    bots_ = std::make_unique<ai::BotController>(sim_, net_->start_info().seed);
    for (const ai::BotSpec& spec : config_.bots) {
        if (spec.seat >= 4 || room.slots[spec.seat].state != net::SlotState::Bot) continue;
        bot_sinks_.push_back(std::make_unique<NetBotSink>(*net_, spec.seat));
        std::string why;
        if (!add_bot(spec, *bot_sinks_.back(), why)) std::cerr << "[Application] No bot at seat " << static_cast<unsigned>(spec.seat) << ": " << why << std::endl;
    }
}

bool Application::add_bot(const ai::BotSpec& spec, sim::CommandSink& sink, std::string& why) {
    if (config_.bot_factory) return bots_->add(spec, config_.bot_factory(spec), sink, why);
    return bots_->add(spec, sink, why);
}

void Application::stop_bots() {
    bots_.reset();                                                    // (the controller first: it holds the sinks)
    bot_sinks_.clear();
}

void Application::quit() {
    if (menu_enabled_) start_menu_.flush();                    // (a name that was typed and not written yet)
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

// The setup screen is created (again): its labels stay empty until its refresh, 500 ms later
void Application::enter_map_select() {
    state_ = AppState::MapSelect;
    update_picture();                                        // (a page of the original's: centred in the canvas)
    closing_click_pending_ = false;                          // (close_quick_help() sets it after this call)
    map_select_.enter();
    // The new screen's buttons are fresh objects in the up state; the INPUT task of the original sends the pointer to the top window in the very input run that
    // follows (FUN_0102653f), so a button under the pointer shows its hover picture before the first frame. Nothing is reset or moved: the pointer is one global.
    if (mouse_has_moved_ && !pointer_outside_) map_select_.handle_mouse_motion(mouse_screen_x_, mouse_screen_y_);
}

// The loading screen ends: the start menu when this run has one, else the screens that follow it
void Application::finish_loading() {
    if (menu_enabled_) {
        enter_start_menu();
        return;
    }
    show_opening_screens();
}

// The quick help when the option asks for it, else the setup screen: what follows the loading screen of a game without a menu, and the menu's Continue
void Application::show_opening_screens() {
    if (hud_.is_quick_help_enabled()) {
        state_ = AppState::QuickHelp;
        update_picture();                                    // (the quick help is a page of the original's, centred; the setup screen of a 960 x 540 canvas is not)
        quick_help_start_.reset();
        if (mouse_has_moved_ && !pointer_outside_) quick_help_start_.on_move(mouse_screen_x_, mouse_screen_y_);       // the pointer goes to the new window at once (see enter_map_select)
    } else {
        enter_map_select();
    }
}

void Application::return_to_map_select() {
    stop_bots();
    if (network_active()) net_end_session(net_notice_);   // leaving a match leaves the room: the local setup screen follows
    net_notice_.clear();
    enter_map_select();
    scorecard_.hide();
    update_picture();
    hud_.close_quit_dialog();
    hud_.close_quick_help();
    hud_.close_options();
    if (background_stepping_) pending_music_ = PendingMusic::Intro;      // (a hidden page starts no music: it does when the page is shown)
    else start_intro_music();                                  // (the pointer is one global in the original: it is neither reset nor moved here)
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
extern "C" void emscripten_main_loop_iter(void* arg) {
    auto* app = static_cast<Application*>(arg);
    if (app && app->is_running()) {
        app->run_frame();
    }
}

// For the page (web/shell.html): its timer wakes a hidden page's network match when the server is quiet (every message of the server wakes it by itself, see
// NetGame::set_on_wake). It does nothing in a page that is shown, and nothing while a step is already running.
extern "C" EMSCRIPTEN_KEEPALIVE void ants_background_pump() {
    if (g_web_app != nullptr) g_web_app->background_pump();
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

// A UI sound. The click of a pressed button (buttonclick.wav, the first frame of its pressed clip) belongs to that button's sprite: it is cut when the button is
// released and its clip is replaced by the raised one (FUN_0102c0db -> StopTracked), so a short click is cut short. The other UI sounds are not tied to a release.
void Application::play_ui_sound(uint32_t sound_id) {
    const uint32_t owner = (sound_id == sim::SoundID::ButtonClick) ? kUiPressOwner : 0u;
    play_effect(sound_id, owner);
}

// A sound effect that no tick makes (the buttons, the can't-go cue): a background step makes none, the page is not heard
void Application::play_effect(uint32_t sound_id, uint32_t owner) {
    if (background_stepping_) return;
    audio_mixer_.play_sfx(sound_id, 1.0f, 255, false, owner);
}

void Application::release_ui_sounds() {
    audio_mixer_.stop_owner(kUiPressOwner);
}

void Application::handle_events() {
    SDL_Event event;
    bool pointer_gone = false;                               // the last event took the pointer away (pointer_gone_after: a release outside the window, a lifted finger)
    // A run with a start menu: a left press or release that changes the screen or the panel begins the rule for the rest of its click sequence (swallow_menu_gesture). The change is looked
    // for when the next event comes and when the events end: the handlers below leave their loop body by `continue`.
    uint32_t screen_before = menu_enabled_ ? screen_signature() : 0u;
    bool left_event = false;
    uint32_t left_event_ms = 0;
    const auto watch_screen = [&]() {
        if (!menu_enabled_) return;
        const uint32_t now_screen = screen_signature();
        if (left_event && now_screen != screen_before) begin_menu_gesture(left_event_ms);
        screen_before = now_screen;
        left_event = false;
    };
    while (SDL_PollEvent(&event)) {
        watch_screen();
        left_event = (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) && event.button.button == SDL_BUTTON_LEFT;
        left_event_ms = left_event ? event.button.timestamp : 0u;
        if (pointer_gone) pointer_outside_ = true;           // ... marked once that event has been handled (its handlers clear the mark)
        update_picture();                                    // (a screen that an earlier event of this frame changed)
        pointer_gone = pointer_gone_after(event, event.type == SDL_MOUSEBUTTONUP && button_outside_window(event.button));   // the position before the clamp
        clamp_pointer_event(event, picture_);                // a pointer over a black bar of a wide window is the pointer on the edge of the picture (pointer_clamp.hpp); the canvas's
                                                             // position becomes the picture's own (the picture's corner subtracted): the 640 x 480 picture centred in a 960 x 540 canvas
        if (event.type == SDL_QUIT) {
            quit();
            return;
        }
        if (event.type == SDL_MOUSEBUTTONUP) release_ui_sounds();                         // (the setup screen takes its mouse events in the loop below)

        if (event.type == SDL_WINDOWEVENT) handle_window_event(event.window);

        // Bypass Print Screen key so OS handles screenshots unimpeded
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_PRINTSCREEN) {
            continue;
        }

        // Alt+Enter: fullscreen on / off (native builds). The key is the window's, not a screen's: no screen sees it (Enter would send the chat text), and a held key toggles once.
        if (event.type == SDL_KEYDOWN && (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_KP_ENTER) && (event.key.keysym.mod & KMOD_ALT) != 0) {
            if (event.key.repeat == 0) toggle_fullscreen();
            continue;
        }

        if (state_ == AppState::Loading) {
            if (event.type == SDL_MOUSEMOTION) note_pointer(event.motion.x, event.motion.y);            // the pointer is global in the original: the next screen knows it
            if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) note_pointer(event.button.x, event.button.y);
            if (event.type == SDL_KEYDOWN || event.type == SDL_MOUSEBUTTONDOWN) {
                finish_loading();
            }
            continue;
        }

        if (state_ == AppState::QuickHelp) {
            if (event.type == SDL_MOUSEMOTION) {
                mouse_screen_x_ = event.motion.x;
                mouse_screen_y_ = event.motion.y;
                mouse_has_moved_ = true;
                pointer_outside_ = false;
                quick_help_move(event.motion.x, event.motion.y);
            } else if (event.type == SDL_MOUSEBUTTONDOWN) {
                mouse_screen_x_ = event.button.x;
                mouse_screen_y_ = event.button.y;
                mouse_has_moved_ = true;
                pointer_outside_ = false;
                if (event.button.button == SDL_BUTTON_LEFT) {
                    if (swallow_menu_gesture(event.button.clicks, event.button.timestamp)) quick_help_move(event.button.x, event.button.y);       // (the second click of the one that opened the quick help)
                    else quick_help_press(event.button.x, event.button.y);
                }
            } else if (event.type == SDL_MOUSEBUTTONUP) {
                mouse_screen_x_ = event.button.x;
                mouse_screen_y_ = event.button.y;
                if (event.button.button == SDL_BUTTON_LEFT) quick_help_release(event.button.x, event.button.y);
            } else if (event.type == SDL_KEYDOWN) {
                quick_help_key(event.key.keysym.sym);
            }
            continue;
        }

        if (state_ == AppState::StartMenu) {
            if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT && swallow_menu_gesture(event.button.clicks, event.button.timestamp)) {
                note_pointer(event.button.x, event.button.y);
                start_menu_.on_mouse_move(event.button.x, event.button.y);              // the pointer is there (a hover); nothing is pressed
                continue;
            }
            handle_menu_event(event);
            continue;
        }

        if (state_ == AppState::MapSelect) {
            switch (event.type) {
                case SDL_KEYDOWN:
                    map_select_.handle_key_down(event.key.keysym.sym, event.key.repeat != 0);
                    break;
                case SDL_MOUSEMOTION:
                    mouse_screen_x_ = event.motion.x;
                    mouse_screen_y_ = event.motion.y;
                    mouse_has_moved_ = true;
                    pointer_outside_ = false;
                    map_select_.handle_mouse_motion(event.motion.x, event.motion.y);
                    break;
                case SDL_MOUSEBUTTONDOWN:
                    mouse_screen_x_ = event.button.x;
                    mouse_screen_y_ = event.button.y;
                    mouse_has_moved_ = true;
                    pointer_outside_ = false;
                    if (event.button.button == SDL_BUTTON_LEFT && swallow_the_rest_of_a_click(event.button.clicks, event.button.timestamp)) {
                        map_select_.handle_mouse_motion(event.button.x, event.button.y);       // the pointer is there (a hover); nothing is pressed
                        break;
                    }
                    map_select_.handle_mouse_down(event.button.x, event.button.y, event.button.button);
                    break;
                case SDL_MOUSEBUTTONUP:
                    mouse_screen_x_ = event.button.x;
                    mouse_screen_y_ = event.button.y;
                    mouse_has_moved_ = true;
                    pointer_outside_ = false;
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
            default:
                break;
        }
    }
    watch_screen();
    if (pointer_gone) pointer_outside_ = true;
}

// Is the position of a button event, as SDL delivered it (before the clamp), outside the window? Not merely outside the picture: the black bars are part of
// the window. SDL gives the position in the picture's coordinates (SDL_RenderSetLogicalSize); SDL_RenderLogicalToWindow (SDL 2.0.18; the game already
// needs 2.0.22 for SDL_HINT_VIDEODRIVER) takes it back to the window's. An event that names no window of ours (windowID 0: SDL had no mouse focus, so the
// pointer was not over the window, and the position was not mapped either; the web build's release after a LEAVE) is outside.
bool Application::button_outside_window(const SDL_MouseButtonEvent& button) const {
    if (window_ == nullptr || button.windowID != SDL_GetWindowID(window_)) return true;
    int w = 0;
    int h = 0;
    SDL_GetWindowSize(window_, &w, &h);
    int wx = button.x;
    int wy = button.y;
    if (renderer_ && renderer_->get_sdl_renderer() != nullptr) {
        SDL_RenderLogicalToWindow(renderer_->get_sdl_renderer(), static_cast<float>(button.x), static_cast<float>(button.y), &wx, &wy);
    }
    return wx < 0 || wy < 0 || wx >= w || wy >= h;
}

void Application::handle_window_event(const SDL_WindowEvent& we) {
    if (we.event == SDL_WINDOWEVENT_FOCUS_LOST) {
        set_app_active(false);
        if (!config_.headless) SDL_ShowCursor(SDL_ENABLE);                     // not ours to hide while another window has the input
    }
    if (we.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
        set_app_active(true);
        if (!config_.headless && !pointer_outside_) SDL_ShowCursor(SDL_DISABLE);
    }
    if (we.event == SDL_WINDOWEVENT_ENTER) {
        pointer_outside_ = false;
        if (!config_.headless) SDL_ShowCursor(SDL_DISABLE);                    // the game's own cursor takes over inside the window
    }
    if (we.event == SDL_WINDOWEVENT_LEAVE) {
        pointer_outside_ = true;                                               // no more motion arrives: the last position must not keep the map scrolling
        if (!config_.headless) SDL_ShowCursor(SDL_ENABLE);
    }
    if (we.event == SDL_WINDOWEVENT_MINIMIZED || we.event == SDL_WINDOWEVENT_HIDDEN) {
        is_paused_ = true;
        midi_player_.pause();
    }
    if (we.event == SDL_WINDOWEVENT_RESTORED || we.event == SDL_WINDOWEVENT_SHOWN) {
        is_paused_ = false;
        midi_player_.resume();
    }
    if (we.event == SDL_WINDOWEVENT_SIZE_CHANGED || we.event == SDL_WINDOWEVENT_RESIZED || we.event == SDL_WINDOWEVENT_MAXIMIZED ||
        we.event == SDL_WINDOWEVENT_RESTORED) {
        if (renderer_ && window_) {
            config_.fullscreen = (SDL_GetWindowFlags(window_) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) != 0;
            renderer_->refit_canvas();                           // SDL's logical size applied again for the window's size now: the picture is scaled to what fits, centred
        }
        update_mouse_grab();
    }
}

// In fullscreen the pointer stays in the window while the game has the focus (wants_mouse_grab, pointer_clamp.hpp): SDL's fullscreen (--fullscreen) and a
// macOS fullscreen Space (os_fullscreen) both ask SDL for the grab, and SDL applies it while the window has the input focus; with a second monitor the
// pointer used to slip away at the edge of the picture, and the edge scrolling stopped. The original's exclusive 640 x 480 mode gives the same on a
// one-monitor machine (the display mode does it: Ants.exe never clips the pointer, and with a second monitor its pointer could leave and its edge scrolling
// stopped, as the remake's did). In a window the pointer is free. Limits: a fullscreen toggle of a Linux window manager (F11) is not detected, so it does
// not grab. On X11, when SDL applies the grab (at the start, on entering fullscreen, when the focus returns) while another client holds the pointer (a
// menu, a screenshot tool), SDL retries XGrabPointer 100 times, 50 ms apart, inside that call: the game can stall for up to 5 s, once, because after a
// failed round SDL stops grabbing for the rest of the run (SDL_GetWindowMouseGrab still answers yes); SDL has no hint to shorten it. The web build never
// grabs: the browser decides (pointer lock). Called at the start and on every size event (the focus needs no call: SDL lets go and grabs again itself);
// macOS reports the entry into and the exit from a Space with SIZE_CHANGED, RESIZED and MAXIMIZED or RESTORED (SDL 2.32's Cocoa listener sends them at the
// end of the transition).
void Application::update_mouse_grab() {
#if !defined(__EMSCRIPTEN__)
    if (window_ == nullptr) return;
    const uint32_t flags = SDL_GetWindowFlags(window_);
    const bool sdl_fullscreen = (flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) != 0;
    const bool space = !config_.headless && os_fullscreen(window_);                          // (a headless run asks nothing of the window system)
    const bool want = wants_mouse_grab(sdl_fullscreen, space, config_.headless);
    const bool asked = (flags & SDL_WINDOW_MOUSE_GRABBED) != 0;                              // the request (SDL_GetWindowMouseGrab is its effect: no while unfocused)
    if (asked != want) SDL_SetWindowMouseGrab(window_, want ? SDL_TRUE : SDL_FALSE);
#endif
}

bool Application::toggle_fullscreen() {
#if defined(__EMSCRIPTEN__)
    return false;                                         // the page's own button does it (a browser asks for the fullscreen itself)
#else
    if (window_ == nullptr) return false;
    const uint32_t flags = SDL_GetWindowFlags(window_);
    const bool sdl_fullscreen = (flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) != 0;
    if (!sdl_fullscreen && !config_.headless && os_fullscreen(window_)) return true;      // a macOS fullscreen Space: the operating system's controls leave it, not SDL's flags
    SDL_SetWindowFullscreen(window_, sdl_fullscreen ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
    config_.fullscreen = !sdl_fullscreen;
    update_mouse_grab();                                  // (the size events that follow do it again)
    return !sdl_fullscreen;
#endif
}

WindowRect Application::window_rect() const {
    WindowRect r;
    if (window_ != nullptr) {
        SDL_GetWindowPosition(window_, &r.x, &r.y);
        SDL_GetWindowSize(window_, &r.w, &r.h);
    }
    return r;
}

// --grid CxR --cell N puts the window into a cell of a grid over the usable part of the display (the start scripts: four games, one per quarter); --window-pos
// and --window-size say it outright. The window's decoration (title bar, frame) is part of the cell.
void Application::apply_window_layout() {
#if !defined(__EMSCRIPTEN__)
    if (window_ == nullptr) return;
    if (config_.grid_cols > 0 && config_.grid_rows > 0) {
        int display = config_.display_index >= 0 ? config_.display_index : SDL_GetWindowDisplayIndex(window_);
        if (display < 0) display = 0;
        SDL_Rect area{0, 0, 0, 0};
        if (SDL_GetDisplayUsableBounds(display, &area) != 0 && SDL_GetDisplayBounds(display, &area) != 0) return;
        int top = 0;
        int left = 0;
        int bottom = 0;
        int right = 0;
        if (SDL_GetWindowBordersSize(window_, &top, &left, &bottom, &right) != 0) {     // a platform that cannot tell: assume a typical title bar
            top = 28;
            left = bottom = right = 0;
        }
        const WindowRect r = grid_cell_window(WindowRect{area.x, area.y, area.w, area.h}, config_.grid_cols, config_.grid_rows, config_.grid_cell, top, left, bottom, right,
                                              canvas_width_of(aspect_), canvas_height_of(aspect_));
        SDL_SetWindowSize(window_, r.w, r.h);
        SDL_SetWindowPosition(window_, r.x, r.y);
        return;
    }
    if (config_.has_window_size) {
        SDL_SetWindowSize(window_, config_.window_w, config_.window_h);
    } else if (aspect_ != Aspect::Classic4x3) {
        // a game of another aspect opens at the largest scale in steps of 0.5 of its canvas that fits the usable area of the display (at least 1x), centred (window_layout.hpp). A game that
        // starts in fullscreen gets the same size: it is the window that Alt+Enter (or the macOS fullscreen button) gives back, and it has the canvas's shape, not the config's 4:3 one
        int display = config_.display_index >= 0 ? config_.display_index : SDL_GetWindowDisplayIndex(window_);
        if (display < 0) display = 0;
        SDL_Rect area{0, 0, 0, 0};
        if (SDL_GetDisplayUsableBounds(display, &area) == 0 || SDL_GetDisplayBounds(display, &area) == 0) {
            int top = 0;
            int left = 0;
            int bottom = 0;
            int right = 0;
            if (!config_.headless && (config_.fullscreen || SDL_GetWindowBordersSize(window_, &top, &left, &bottom, &right) != 0)) {      // a fullscreen window has no borders to measure, nor has a platform that cannot tell: assume a typical title bar
                top = 28;
                left = bottom = right = 0;
            }
            const WindowRect r = default_canvas_window(WindowRect{area.x, area.y, area.w, area.h}, canvas_width_of(aspect_), canvas_height_of(aspect_), top, left, bottom, right);
            SDL_SetWindowSize(window_, r.w, r.h);                 // (in fullscreen SDL keeps it as the windowed size, for the way back)
            if (!config_.has_window_pos) SDL_SetWindowPosition(window_, r.x, r.y);
        }
    }
    if (config_.has_window_pos) SDL_SetWindowPosition(window_, config_.window_x, config_.window_y);
#endif
}

// The INPUT task of the original runs every 50 ms (Ants.exe 0x100ae26): the edge strips and the minimap drag move the view in whole pixel
// steps at that rate (edge_scroll.hpp). Panning is strictly mouse-driven; keyboard keys do not scroll.
void Application::handle_camera_panning(float dt) {
    if (state_ == AppState::MapSelect || scorecard_.is_open()) return;
    input_accumulator_ += dt;
    while (input_accumulator_ >= 0.050f) {
        input_accumulator_ -= 0.050f;
        if (state_ != AppState::Playing || !renderer_ || !mouse_has_moved_ || pointer_outside_) continue;
        if (mouse_screen_x_ < 0 || mouse_screen_x_ >= layout_.width || mouse_screen_y_ < 0 || mouse_screen_y_ >= layout_.height) continue;
        hud_.input_tick(renderer_->camera(), current_level_.width(), current_level_.height(), mouse_screen_x_, mouse_screen_y_);
    }
}

void Application::handle_key_down(const SDL_KeyboardEvent& key) {
    if (scorecard_.is_open()) {
        // FUN_01015b17: Enter and the letters C, Q and X (either case, whatever the modifiers) leave, at any time; nothing else does anything (Esc included)
        const SDL_Keycode sym = key.keysym.sym;
        if (sym == SDLK_RETURN || sym == SDLK_KP_ENTER || sym == SDLK_c || sym == SDLK_q || sym == SDLK_x) scorecard_.leave();
        return;
    }

    bool ctrl_or_gui = (key.keysym.mod & KMOD_CTRL) || (key.keysym.mod & KMOD_GUI);

    // Only the original's keys exist (HUD::handle_key_down, FUN_0102609a): there are no screenshot, tile grid, mute, team switch or fullscreen keys.

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
    pointer_outside_ = false;

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
    pointer_outside_ = false;
    if (button.type == SDL_MOUSEBUTTONUP) release_ui_sounds();                          // before the handlers: what the release itself plays is not cut

    if (scorecard_.is_open()) {
        if (button.button != SDL_BUTTON_LEFT) return;                      // the button class knows the left button only (FUN_01011206): the screen swallows the others
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
                finish_loading();
                break;
            }
        }
        return;
    }
    if (state_ == AppState::QuickHelp) {
        return;
    }

    if (state_ == AppState::MapSelect || state_ == AppState::StartMenu || is_paused_) {
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
    audio_mixer_.set_listener_position(renderer_->camera().world_x + layout_.view().w / 2,
                                       renderer_->camera().world_y + layout_.view().h / 2);

    if (renderer_) {
        renderer_->update_transient_effects(dt);
    }

}

// What one simulation tick shows: the HUD, the events of the tick, the sounds and, once, the end of the match.
void Application::post_tick() {
    const auto& world = sim_.get_world_state();
    hud_.update(world, 1);
    hud_.poll_sim_events(sim_);

    if (page_hidden_) ++hidden_ticks_;                       // (the console's line about a hidden period says how far the match went)
    auto audio_events = sim_.poll_audio_events();            // (drained in every case: the queue must not grow)
    if (!background_stepping_) audio_mixer_.ingest_simulation_events(audio_events, local_player_id_);   // a background step makes no sound: its events are dropped, not saved up

#if defined(__EMSCRIPTEN__)
    // A page that embeds several games (web/four.html) shows that the machines stay in step: every 100 ticks the game tells its parent page the tick and the
    // state hash (the page compares the hashes of the same tick)
    if (network_active() && sim_.current_tick() % 100 == 0) {
        const sim::StateHash h = sim_.state_hash();
        ants_post_sync_to_parent(static_cast<int>(local_player_id_), static_cast<int>(sim_.current_tick()), static_cast<int>(h.total >> 32), static_cast<int>(h.total & 0xFFFFFFFFu));
    }
#endif

    check_match_over();
    if (bots_ && !sim_.is_match_over()) bots_->on_tick(sim_);        // the computer players, last: they act on the world that this tick made
}

void Application::check_match_over() {
    if (!sim_.is_match_over() || match_over_handled_) return;
    match_over_handled_ = true;
    const auto& world = sim_.get_world_state();
    scorecard_.show(world.match_result, local_player_id_);       // "Waiting for scores..."; the cue plays when the rows appear (update_scorecard)
    update_picture();                                            // the results are a page of the original's
    if (background_stepping_) pending_music_ = PendingMusic::Closed;     // (a hidden page changes no sound: the music closes when the page is shown)
    else close_music();                                          // FUN_010226da closes the music sequencer at once (0x1022714); nothing restarts it
    music_resume_on_activate_ = false;
    if (net_) net_->freeze();                                    // the host stops sealing turns
}

// Once per frame: a match that has ended without a tick of this application (a command, a drop-out) opens the results screen too; the screen's clock builds its
// rows, and with them the one cue of the machine, 250 ms after it opened
void Application::update_results(float dt) {
    update_picture();                                            // (the results screen is a page of the original's, whoever opened it)
    if (state_ == AppState::Playing) check_match_over();
    if (!scorecard_.is_open()) return;
    scorecard_.update(dt);
    if (mouse_has_moved_ && !pointer_outside_) scorecard_.handle_mouse_motion(mouse_screen_x_, mouse_screen_y_);   // the INPUT task's poll: the Leave button that appears under a resting pointer hovers
    const uint32_t cue = scorecard_.get_audio_to_play();
    if (cue > 0) {
        audio_mixer_.play_sfx(cue, 1.0f, 255);
        scorecard_.clear_audio_to_play();
    }
}

// FUN_0101453f: the quit dialog's Yes. With exactly one other side left (FUN_0100c5b1) the quit is the end of the match: the game-over message names the
// quitter, whose row goes last on every results screen, and the results (with Leave) follow. With more sides left the original sends the drop message and
// exits; here the player leaves the same way as before (the network announces the departure to the others).
void Application::confirm_quit() {
    if (state_ == AppState::Playing && !sim_.is_match_over() && sim_.other_sides(local_player_id_) == 1) {
        sim::Command quit_command;
        quit_command.type = sim::CommandType::Quit;
        quit_command.issuer = local_player_id_;
        if (network_active()) {
            net_->submit(quit_command);                          // it reaches the simulation with the turn that carries it, on every machine alike
        } else {
            sim_.apply_command(quit_command);
            check_match_over();
        }
        return;
    }
    leave_game();                                                // (a network game that the start menu led to: back to the menu)
}

// ------------------------------------------------------------------------------------------------
// Network play (docs/NETWORK_PORT.md): the setup screen is the room, the lock-step runner drives the ticks
// ------------------------------------------------------------------------------------------------

void Application::pump_network(float dt, double gap_seconds) {
    if (menu_enabled_) update_start_menu(dt);                    // the start menu's clock, what it asked for and its connection (application_menu.cpp)
    if (!net_ || !net_->active()) return;
    net_time_ms_ += static_cast<double>(dt) * 1000.0;
    net_->update(static_cast<uint32_t>(net_time_ms_));        // (the connection is read here: whatever waited counts as heard)
    if (gap_seconds > 0.0) net_->note_gap(static_cast<uint32_t>(std::min(gap_seconds * 1000.0, 4.0e9)));   // a host that said nothing has been silent for the gap too
    handle_net_events();
    if (!net_) return;                                           // (a lost game brought the player back to the start menu, which let go of the net)
    if (menu_enabled_ && state_ == AppState::MapSelect && (net_->phase() == net::NetGame::Phase::Failed || net_->phase() == net::NetGame::Phase::Over)) {
        return_to_start_menu(net_->status_text());               // the room is dead (the server closed it, the connection is gone): not a room screen that nothing can happen on
        return;
    }
    if (state_ == AppState::MapSelect && net_->active()) {
        sync_room_view();
        // --start-when N (a test hook): the leader of a server's room presses START, as a click or the S key would, once N players are in; again every second until the match starts
        if (config_.net_start_when > 0 && net_->is_leader() && !map_select_.is_locked() && net_time_ms_ - start_when_pressed_ms_ >= 1000.0) {
            size_t players = 0;
            for (const auto& slot : net_->room().slots) players += slot.state != net::SlotState::Empty ? 1u : 0u;
            if (players >= config_.net_start_when) {
                start_when_pressed_ms_ = net_time_ms_;
                map_select_.handle_key_down(SDLK_s);
            }
        }
    }
}

void Application::sync_room_view() {
    if (!net_ || !net_->active()) return;
    MapSelectScreen::RoomView view;
    view.networked = true;
    view.is_host = net_->is_host();
    view.leader = net_->is_leader();                                         // the first player of a server's room: the host's screen with START (protocol 7)
    view.my_seat = net_->my_seat();
    const net::RoomMsg& room = net_->room();
    for (size_t i = 0; i < view.seats.size(); ++i) {
        view.seats[i].occupied = room.slots[i].state != net::SlotState::Empty;
        view.seats[i].name = room.slots[i].name;
        view.seats[i].thumb = static_cast<MapSelectScreen::Thumb>(static_cast<uint8_t>(net_->seat_quality(static_cast<uint8_t>(i))));   // connection quality
    }
    // A guest that has not been welcomed yet has no seat and the room is not known: it shows ITSELF alone (the original's slot 0 is the local machine from the
    // start, with the good thumb; the host's row follows once the connection exists), in the colour of its seat (or of the seat it asked for)
    if (net_->phase() == net::NetGame::Phase::Connecting || net_->phase() == net::NetGame::Phase::Failed) {
        for (auto& seat : view.seats) seat = MapSelectScreen::RoomSeat{};
        const uint8_t assigned = net_->my_seat();                              // (known once the host has welcomed it; the requested seat is only a guess before that)
        const uint8_t own = assigned < 4 ? assigned : (config_.net_seat < 4 ? config_.net_seat : (local_player_id_ < 4 ? local_player_id_ : static_cast<uint8_t>(0)));
        view.my_seat = own;
        view.seats[own].occupied = true;
        view.seats[own].name = player_name_;
        view.seats[own].thumb = MapSelectScreen::Thumb::Good;
    }
    view.map_file = room.map_name;                                           // a guest (and the leader of a server's room) shows the host's / the room's choice (an empty name before the first message)
    view.status = net_->status_text();
    map_select_.set_room(view);
    if (!net_->is_host() && net_->phase() == net::NetGame::Phase::Room) {
        map_select_.follow_host_choice(room.map_name, room.fog);
    }
}

// START on the leader's screen: the request goes to the server, which starts the match with the players who are in the room if it can. With fewer than two players the
// answer is the host's: the can't-go cue (and nothing else: no message, no change of the screen); the room of the server has no thumbs to wait for.
void Application::net_request_start() {
    if (!net_ || !net_->request_start()) play_effect(sim::SoundID::CantGo);
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
        play_effect(sim::SoundID::CantGo);                                   // not enough players yet
        return;
    }
    map_select_.lock();                                                      // START ran: the map and the fog option are fixed (+0x130)
}

void Application::handle_net_events() {
    if (!net_) return;
    for (const net::NetGame::Event& ev : net_->take_events()) {
        switch (ev.type) {
            case net::NetGame::Event::Type::StartRequested:
                map_select_.lock();                                  // the start is on (a host's START locks the screen itself; the leader's START is answered by this Start): nothing more to press
                net_load_match();
                break;
            case net::NetGame::Event::Type::Begun:
                net_begin_match();
                break;
            case net::NetGame::Event::Type::Cancelled:               // the start failed: back in the room, the host's controls work again
                map_select_.unlock();
                break;
            case net::NetGame::Event::Type::HostLeft:                // the host is gone and no other machine could take over (or the server dropped this player for being away)
                net_notice_ = net_->status_text();                   // the reason, as the network layer says it: "The connection to the other players was lost." or "You were away too long ..."
                std::cerr << "[Application] " << net_notice_ << std::endl;                    // (for the scripts that run a headless client: nothing else says it)
                if (state_ == AppState::Playing && !scorecard_.is_open()) {
                    if (menu_enabled_) {
                        return_to_start_menu(net_notice_);                   // (the net is gone: the rest of the events belong to it)
                        return;
                    }
                    return_to_map_select();
                }
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
    if (path.empty()) path = config_.maps_dir + "/" + start.map_name;
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
    start_net_bots();
}

// The session is over (the player left, the host left, a match ended and its results were closed): back to the local setup screen.
void Application::net_end_session(const std::string& notice) {
    stop_bots();
    if (net_) net_->leave();
    hud_.set_command_sink(nullptr);
    hud_.set_roster_mask(0x0F);
    MapSelectScreen::RoomView local;
    local.status = notice;                                    // a notice stays on the setup screen until the next action
    map_select_.set_room(local);
    const uint8_t own_team = config_.local_player_id < 4 ? config_.local_player_id : static_cast<uint8_t>(0);      // the local game plays the configured team again, not the seat of the match
    local_player_id_ = own_team;
    map_select_.set_player_team(own_team);
    apply_team_names(config_.team_names, 0x0F);
}

// The waiting and out-of-sync messages of a network match (remake UI: the original has no such text). A machine that waits for the next turn
// says so after one second; a desync stops the match and says so.
void Application::render_net_overlay() {
    if (!network_active() || net_->phase() != net::NetGame::Phase::Playing) return;
    NetOverlayInput in;
    in.desynced = net_->desynced();
    in.electing = net_->electing();
    in.stalled_ms = net_->stalled_ms();
    const uint8_t slow = net_->laggard();
    if (slow < 4) in.waiting_for = sim_.get_player_name(slow);
    in.catching_up = net_->catching_up();
    if (const std::optional<uint32_t> self_lag = net_->self_lag_behind_ms()) in.self_lag_behind_ms = *self_lag;
    if (const std::optional<net::NetGame::LagNotice> lag = net_->lag_notice()) {
        in.lag_seat = lag->seat;
        in.lag_name = sim_.get_player_name(lag->seat);
        in.lag_behind_ms = lag->behind_ms;
    }
    in.notice = net_->match_notice();                                   // "Bob is the host now." for a few seconds
    const NetOverlayLine line = net_overlay_line(in);
    const std::string& text = line.text;
    const ants::assets::ColorRGBA colour = line.alarm ? ants::assets::ColorRGBA{255, 90, 90, 255} : ants::assets::ColorRGBA{255, 255, 255, 255};
    if (text.empty()) return;
    const int32_t w = renderer_->get_text_width(text, FontSize::Px14);
    const int32_t h = renderer_->get_text_height(FontSize::Px14);
    const NetOverlayBox where = net_overlay_box(layout_.view(), w, h);   // centred in the map view, 5 rows below its top (the original's (17 + (441 - w) / 2, 26))
    renderer_->fill_rect(where.box.x, where.box.y, where.box.w, where.box.h, ants::assets::ColorRGBA{0, 0, 0, 170});
    renderer_->draw_text(text, where.text_x, where.text_y, colour, FontSize::Px14);
}

// The network's part of the corner (ants_app/latency_corner.hpp): "ping NN ms" and "delay NN ms" while a room or a match of a network game is on screen. A game of one
// machine, the loading and quick help screens, a connection that is being made, one that failed or is over draw nothing here.
void Application::render_latency_corner(int32_t version_x, int32_t text_y, const CornerPlate& plate) {
    if (!network_active()) return;
    const CornerScreen screen = state_ == AppState::MapSelect ? CornerScreen::Setup
                                : state_ == AppState::Playing ? (scorecard_.is_open() ? CornerScreen::Results : CornerScreen::Match)
                                                              : CornerScreen::Other;
    const std::optional<int32_t> left_limit = latency_left_limit(true, net_->phase(), screen, layout_);
    if (!left_limit) return;
    LatencyReadout readout;
    readout.ping_ms = net_->ping_ms();
    readout.delay_ms = net_->command_delay_ms();
    last_latency_layout_ = latency_corner_layout(*renderer_, readout, version_x, text_y, *left_limit, plate);
    draw_latency_corner(*renderer_, readout, version_x, text_y, *left_limit, plate);
}

void Application::render_frame() {
    renderer_->begin_frame();
    last_latency_layout_.reset();                        // (set again when this frame draws the network's readout)
    update_picture();
    if (picture_ != canvas().rect()) {                   // a page of the original's own screen in a bigger canvas: the clay of its pages fills what is around it
        renderer_->set_picture(canvas().rect());
        renderer_->fill_rect(0, 0, renderer_->canvas_w(), renderer_->canvas_h(), kPageMargin);
    }
    renderer_->set_picture(picture_);                    // the screens are the picture (the match: the whole canvas; a page of the original's: centred in it)

    if (state_ == AppState::Loading) {
        render_loading_screen();
    } else if (state_ == AppState::QuickHelp) {
        render_quick_help_screen();
    } else if (state_ == AppState::StartMenu) {
        render_start_menu(*renderer_, assets_, start_menu_);
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

    // Frame rate counter and frametime sparkline in the bottom right hand corner of the canvas (the viewport: the version stands next to the counter in every layout):
    // the plate is the canvas's, not the picture's
    renderer_->set_picture(canvas().rect());
    const CornerPlate plate = CornerPlate::for_canvas(renderer_->canvas_w(), renderer_->canvas_h());
    int fps_val = std::max(1, static_cast<int>(std::round(fps_display_value_)));
    std::string fps_text = std::to_string(fps_val) + " FPS";
    int32_t text_w = renderer_->get_text_width(fps_text, FontSize::Px12);
    int32_t text_h = renderer_->get_text_height(FontSize::Px12);
    constexpr int32_t spark_w = static_cast<int32_t>(SPARKLINE_SAMPLES);
    constexpr int32_t spark_h = FPS_OVERLAY_SPARK_H;
    std::string ver_text(ants::VERSION_STRING);
    int32_t ver_w = renderer_->get_text_width(ver_text, FontSize::Px12);
    const CornerRow row = CornerRow::of(plate, text_w, ver_w, spark_w, spark_h, text_h);          // (the version, the sparkline and the frame rate: one row from the right edge)
    const int32_t text_x = row.fps_x;
    const int32_t spark_x = row.spark_x;
    const int32_t spark_y = row.spark_y;
    const int32_t text_y = row.text_y;
    renderer_->draw_text(fps_text, text_x, text_y, {255, 255, 255, 255}, FontSize::Px12);

    // Version number display (bottom-right next to FPS sparkline)
    const int32_t ver_x = row.version_x;
    const int32_t ver_y = text_y;
    renderer_->draw_text(ver_text, ver_x, ver_y, {180, 190, 200, 220}, FontSize::Px12);

    // Dark translucent background plate + subtle border
    renderer_->fill_rect(spark_x - 1, plate.top, spark_w + 2, plate.bottom - plate.top, ants::assets::ColorRGBA{0, 0, 0, 160});
    renderer_->draw_rect(spark_x - 1, plate.top, spark_w + 2, plate.bottom - plate.top, ants::assets::ColorRGBA{80, 85, 90, 180});

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

    render_latency_corner(ver_x, text_y, plate);

    // Authentic Software Cursor (Matching Ants.exe 0x1026c5c / 0x1027e65): the pointer is the picture's
    renderer_->set_picture(picture_);
    CursorType cur = CursorType::Normal;
    if (state_ == AppState::Playing && !scorecard_.is_open()) {
        cur = hud_.evaluate_cursor(mouse_screen_x_, mouse_screen_y_, sim_.get_world_state(), sim_.grid(), renderer_->camera());
    }
    if (!pointer_outside_) renderer_->render_software_cursor(cur, mouse_screen_x_, mouse_screen_y_, static_cast<uint32_t>(sim_.current_tick()));

    renderer_->end_frame();
}

// FUN_0100e6ce: the intro (intro.mid, `play AntsMidi from 0 notify`) plays ONCE; its end (MM_MCINOTIFY) starts the random in-game pieces (update_music).
void Application::start_intro_music() {
    music_open_ = true;
    midi_player_.load_file(config_.midi_path);
    audio_mixer_.play_music("Original-Ants/INTRO.mp3", false);
    if (config_.headless) {
        midi_player_.play(false);
    }
    if (music_held_) hold_music(true);                       // a window without the focus starts nothing audible
}

// `close AntsMidi`: the device is closed and stays closed until a match start, a piece's end is not waited for any more, the activation of the program or the music slider starts it again
void Application::close_music() {
    music_open_ = false;
    audio_mixer_.stop_music();
    midi_player_.stop();
}

// FUN_0100e6da: a random in-game piece: rand() % 3, never the one that played last (the first pick is uniform: the stored index starts at 3)
void Application::play_next_ingame_music() {
    static const std::string IN_GAME_TRACKS[3] = {
        "ANTS2A",
        "ANTS2B",
        "ANTSFUN3"
    };

    int track = std::rand() % 3;
    if (track == last_music_track_) {
        track = (track + 1) % 3;
    }
    last_music_track_ = track;

    music_open_ = true;
    audio_mixer_.play_music("Original-Ants/" + IN_GAME_TRACKS[track] + ".mp3", false);
    midi_player_.load_file("Original-Ants/" + IN_GAME_TRACKS[track] + ".MID");
    if (config_.headless) {
        midi_player_.play(false);
    }
    if (music_held_) hold_music(true);
}

// MM_MCINOTIFY (0x100e8cc): when the piece that plays has ended, the next random in-game piece starts, on the setup screen as well as in a match
void Application::update_music(float dt) {
    audio_mixer_.update_music(dt);
    midi_player_.update(dt);
    if (music_open_ && !music_held_ && !audio_mixer_.is_music_playing() && !midi_player_.is_playing()) {       // (a held piece has not ended: it is paused)
        play_next_ingame_music();
    }
}

// WM_ACTIVATEAPP: deactivating closes the music (and remembers that it was open), activating starts a NEW random piece.
// --audio-focus (several games on one machine, the start scripts; the original ran one game per machine): a window without the input focus is silent, and its music
// is HELD, not closed: with the original's rule every click on another window would close a piece and the click back would start a new one, so that no piece
// is ever heard to its end. The piece goes on where it was left when the window has the focus again; a piece that starts meanwhile (a match begins in a
// window behind the others) starts held.
void Application::set_app_active(bool active) {
    if (config_.audio_follows_focus) {
        audio_mixer_.set_sound_volume(active ? hud_.get_sound_volume() : 0);      // every sound, playing or new, is silent without the focus
        hold_music(!active);
        return;
    }
    if (!active) {
        if (music_open_) {
            music_resume_on_activate_ = true;
            close_music();
        }
    } else if (music_resume_on_activate_) {
        music_resume_on_activate_ = false;
        play_next_ingame_music();
    }
}

void Application::hold_music(bool hold) {
    music_held_ = hold;
    if (hold) {
        audio_mixer_.pause_music();
        midi_player_.pause();
    } else {
        audio_mixer_.resume_music();
        midi_player_.resume();
    }
}

void Application::render_loading_screen() {
    // 1. Fill the entire page (the original's 640x480 screen) with authentic solid orange #DB4B13
    renderer_->fill_rect(0, 0, ScreenLayout::kClassicWidth, ScreenLayout::kClassicHeight, ants::assets::ColorRGBA{219, 75, 19, 255});

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
    draw_animation_frame0(*renderer_, assets_, quick_help_start_.pressed() ? "qh_start3" : (quick_help_start_.hovered() ? "qh_start2" : "qh_start1"));
}

// The pointer is one global in the original (GetCursorPos): every screen's events record it, and the next screen replays it into its buttons
void Application::note_pointer(int32_t x, int32_t y) {
    mouse_screen_x_ = x;
    mouse_screen_y_ = y;
    mouse_has_moved_ = true;
    pointer_outside_ = false;
}
// The START! button of the quick help is the original's button class: the press captures it (qh_start3 carries no sound), the release runs the callback
// (FUN_01014802: the window closes and the setup screen is created) while the capture is still there, leaving cancels it for good
void Application::quick_help_move(int32_t x, int32_t y) {
    note_pointer(x, y);
    quick_help_start_.on_move(x, y);
}
void Application::quick_help_press(int32_t x, int32_t y) {
    note_pointer(x, y);
    quick_help_start_.on_press(x, y);
}
void Application::quick_help_release(int32_t x, int32_t y) {
    note_pointer(x, y);
    if (quick_help_start_.on_release(x, y)) close_quick_help(true);
}

// The quick help is over. Its button is let go of (a key can close the window while the button is held: nothing stays pressed in a window that is gone), and the setup
// screen takes over with fresh buttons: a release of a press that began on the quick help finds no pressed button there and does nothing. A click that closed the window may
// be the first of a double click (see closing_click_pending()).
void Application::close_quick_help(bool by_click) {
    quick_help_start_.reset();
    enter_map_select();
    closing_click_pending_ = by_click && joined_a_room();
    closing_click_ms_ = SDL_GetTicks();
}

// A left press on the setup screen. After a click closed the quick help of a machine that joined a room, a press that continues that click is not for the screen: the second
// click of a double click on START!, which SDL counts (`clicks` above 1) or which comes within SDL's double-click time of the click that closed the quick help (a press that was
// queued before that click was handled has a time before it, which counts as within). The first press that does neither begins a new click and ends the rule.
bool Application::swallow_closing_click(uint8_t clicks, uint32_t timestamp_ms) {
    if (!closing_click_pending_) return false;
    const bool within = static_cast<int32_t>(timestamp_ms - closing_click_ms_) <= static_cast<int32_t>(kDoubleClickMs);
    if (clicks > 1 || within) return true;
    closing_click_pending_ = false;
    return false;
}

// The setup screen's left press: the rest of the click that closed the quick help of a joined room, or of the click that left the start menu (both rules are asked: each ends by itself)
bool Application::swallow_the_rest_of_a_click(uint8_t clicks, uint32_t timestamp_ms) {
    const bool closing = swallow_closing_click(clicks, timestamp_ms);
    const bool gesture = swallow_menu_gesture(clicks, timestamp_ms);
    return closing || gesture;
}

// FUN_010147c2: Enter (0x18), Esc (0x1a), C (0x43), X (0x58), c (0x63) and x (0x78) run the same callback; M and m open the More Help dialog (not built); no other key does anything
void Application::quick_help_key(SDL_Keycode sym) {
    if (state_ != AppState::QuickHelp) return;
    if (sym == SDLK_RETURN || sym == SDLK_KP_ENTER || sym == SDLK_ESCAPE || sym == SDLK_c || sym == SDLK_x) close_quick_help(false);
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
        show_start_view();
    }
}

// The view at the start (docs 5.44): from the map's corner just far enough to show the square around the anchor tile of the local team's hill (edge_scroll.hpp)
void Application::show_start_view() {
    if (!renderer_) return;
    int32_t tx = 0;
    int32_t ty = 0;
    if (const auto* base = sim_.grid().find_anthill(local_player_id_)) {
        tx = base->x + 1;                                    // the anchor is one tile in from the corner of the 4 x 4 footprint (every shipped map)
        ty = base->y + 1;
    } else if (!current_level_.anthill_spawns.empty()) {
        tx = current_level_.anthill_spawns[0].x;
        ty = current_level_.anthill_spawns[0].y;
    }
    int32_t ox = 0;
    int32_t oy = 0;
    start_view_origin(tx, ty, static_cast<int32_t>(current_level_.width()), static_cast<int32_t>(current_level_.height()), ox, oy, layout_);
    ViewportCamera& camera = renderer_->camera();
    camera.x = 0.0f;
    camera.y = 0.0f;
    camera.scroll_pixels(ox, oy, current_level_.width(), current_level_.height());
}

} // namespace ants::app
