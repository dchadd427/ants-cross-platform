// The fixtures of the start menu application tests (test_start_menu_app.cpp holds main(); the cases are in test_start_menu_app_play.cpp and test_start_menu_app_options.cpp).
// Inline or in an anonymous namespace.
#pragma once
//   test_start_menu_app --probe H:P             one join attempt with a room code that does not exist, against any server (beta.playants.org:4001): it must answer, never hang
#include "ants_app/application.hpp"
#include "ants_app/host_lookup.hpp"
#include "ants_app/start_menu.hpp"
#include "ants_app/version.hpp"
#include "ants_net/netgame.hpp"
#include "ants_net/protocol.hpp"
#include "ants_net/tcp.hpp"
#include "ants_server/map_store.hpp"
#include "ants_server/room.hpp"
#include "ants_server/room_manager.hpp"
#include "ants_sim/game_strings.hpp"
#include "ants_sim/sim_engine.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include "ants_test_paths.hpp"
#include "../common/ants_test_pause.hpp"
#include "../common/test_assert.hpp"
#include "ants_ai/bot_names.hpp"

using namespace ants;
using namespace ants::app;

// The name that the server's room with this code gives the bot of a seat (protocol 20): "Pip (bot, Hard)"; `level` is Easy, Medium or Hard
inline std::string bn(const std::string& code, unsigned seat, const std::string& level) {
    return ai::bot_label(ai::bot_seat_name(ai::bot_name_key(code), static_cast<uint8_t>(seat)), level);
}

namespace fs = std::filesystem;

inline int g_test_count = 0;
inline int g_test_failures = 0;
inline int g_assert_count = 0;

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
    // ANTS_TEST_FILTER=text runs only the cases whose title contains the text (for working on one test and for mutation runs; the suite as run_tests.sh runs it has no filter)
    if (const char* filter = std::getenv("ANTS_TEST_FILTER")) {
        if (name.find(filter) == std::string::npos) return;
    }
    ++g_test_count;
    std::cout << "  RUNNING: " << std::left << std::setw(110) << name << " ... " << std::flush;
    const int prev = g_test_failures;
    try {
        fn();
    } catch (const std::exception& e) {
        std::cout << "FAILED! Exception: " << e.what() << "\n";
        ++g_test_failures;
        return;
    }
    if (g_test_failures == prev) std::cout << "PASS\n";
}

// An assertion on a text that shows the text when it fails
#define ASSERT_HAS(text, needle) \
    do { \
        ++g_assert_count; \
        const std::string text_ = (text); \
        const std::string needle_ = (needle); \
        if (text_.find(needle_) == std::string::npos) { \
            std::cout << "FAILED!\n    '" << text_ << "' does not contain '" << needle_ << "' at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_test_failures; \
            return; \
        } \
    } while (0)

namespace {

inline std::string maps_dir() { return std::string(ORIGINAL_ASSETS_DIR) + "/Maps/"; }

// A folder of the test's own (settings files, screenshots): removed at the end of the run
struct TempDir {
    fs::path path;
    TempDir() {
        std::error_code ec;
        path = fs::temp_directory_path(ec) / ("ants_menu_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(path, ec);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    std::string file(const std::string& name) const { return (path / name).string(); }
};

inline std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

// What a test's name lookup does (kept alive by the lookup's worker thread, which may outlive the test: hence shared)
struct Lookup {
    std::atomic<int> calls{0};
    std::atomic<bool> release{false};       // a lookup that hangs until it is released
    std::atomic<int> mode{0};               // 0: hangs until released, then finds 127.0.0.1; 1: fails
};

inline HostLookup::Resolver lookup_of(const std::shared_ptr<Lookup>& state) {
    return [state](const std::string&, std::string& address, std::string& error) {
        ++state->calls;
        if (state->mode == 1) {
            error = "no such name";
            return false;
        }
        for (int i = 0; i < 6000 && !state->release; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        address = "127.0.0.1";
        return true;
    };
}

// ---- the other machine of a test: a simulation and a NetGame, with the little that the application does for the room ---------------------------------------------------------

struct Peer {
    sim::SimulationEngine sim;
    net::NetGame net{sim};
    std::vector<net::NetGame::Event> events;
    uint32_t now{1000};

    Peer() { net.set_discovery(0); }
    void update() {
        net.update(now);
        for (const auto& ev : net.take_events()) {
            events.push_back(ev);
            if (ev.type == net::NetGame::Event::Type::StartRequested) {
                const net::StartMsg& s = net.start_info();
                assets::LevelData level;
                uint64_t hash = 0;
                const bool ok = level.load_lvl(maps_dir() + s.map_name) && net::hash_file(maps_dir() + s.map_name, hash) && hash == s.map_hash;
                if (ok) {
                    sim.set_fog_of_war_enabled(s.fog);
                    sim.set_game_mode(s.game_mode());                         // (the mode of the Start, before the first init: what the application does)
                    sim.init(level, s.seed, s.roster);
                    sim::apply_start_teams(sim, s.teams());
                }
                net.report_loaded(ok);
            }
        }
    }
    bool join(const std::string& host, uint16_t port, const std::string& name, const std::string& room) { return net.join(host, port, name, 255, room); }
    // A Hello that carries a create block (protocol 15): the server makes the room for it when it offers public rooms and has none of this code (the map's file, "" for the server's own, and the seats)
    bool join_creating(const std::string& host, uint16_t port, const std::string& name, const std::string& room, const std::string& map, uint8_t seats) {
        net::CreateBlock block;
        block.map_name = map;
        block.seats = seats;
        net.set_create(block);
        return join(host, port, name, room);
    }
};

// ---- a dedicated server in the test: the real room manager behind a real TCP listener ----------------------------------------------------------------------------------

inline server::ServerLimits demo_limits(size_t demo_rooms, const std::vector<std::string>& maps, bool hold_seats) {
    server::ServerLimits limits;
    limits.demo_rooms = demo_rooms;
    limits.demo_map = "TINY.LVL";
    limits.demo_maps = maps;
    limits.reconnect = hold_seats;
    return limits;
}
const std::vector<std::string> kAllMaps = {"TINY.LVL", "SMALL.LVL", "MEDIUM.LVL", "GAUNTLET.LVL", "TREASURE.LVL", "ISLANDS.LVL"};

struct Server {
    server::RoomManager mgr;
    std::unique_ptr<net::TcpListener> listener{net::TcpListener::listen(0, true)};
    uint32_t now{1000};

    // `maps`: the maps that the server's public rooms may be made on (--demo-maps; the menu offers the six of the original, a server may offer fewer). `hold_seats`: whether its rooms hold the seat of a
    // lost connection: the server's default since release B; false is a server that was started with --no-reconnect
    explicit Server(size_t demo_rooms = 4, const std::vector<std::string>& maps = kAllMaps, bool hold_seats = server::kReconnectByDefault) : mgr(server::MapStore(std::string(ORIGINAL_ASSETS_DIR) + "/Maps"), demo_limits(demo_rooms, maps, hold_seats)) {}
    uint16_t port() const { return listener ? listener->port() : uint16_t{0}; }
    void update() {
        if (listener) {
            for (int k = 0; k < 4; ++k) {
                auto c = listener->accept();
                if (!c) break;
                mgr.add_connection(std::move(c), "127.0.0.1", now);
            }
        }
        mgr.update(now);
    }
    server::RoomStatus status(const std::string& code) {
        server::RoomStatus s;
        mgr.status(code, s, now);
        return s;
    }
    // A room that is not a public room (the control interface makes these): `players` seats on the map
    bool make_room(const std::string& code, uint8_t players, const std::string& map = "TINY.LVL") {
        server::RoomSpec spec;
        spec.code = code;
        spec.map = map;
        spec.players = players;
        spec.early_start = true;
        spec.has_seed = true;
        spec.seed = 4242;
        return mgr.create_room(spec, now).ok;
    }
    std::string address() const { return "127.0.0.1:" + std::to_string(port()); }
};

// A server that is not one: it accepts, reads the Hello and then does what it was told (answers with a Reject of a reason, hangs up, or says nothing at all), and keeps what it heard
struct RawServer {
    enum class Mode { Reject, HangUp, Silent };
    Mode mode{Mode::Silent};
    net::RejectReason reason{net::RejectReason::Full};
    std::unique_ptr<net::TcpListener> listener{net::TcpListener::listen(0, true)};
    struct Client {
        std::unique_ptr<net::TcpConnection> conn;
        bool answered{false};
    };
    std::vector<Client> clients;
    int accepted{0};
    std::vector<net::HelloMsg> hellos;

    uint16_t port() const { return listener ? listener->port() : uint16_t{0}; }
    void update() {
        if (!listener) return;
        for (int k = 0; k < 4; ++k) {
            auto c = listener->accept();
            if (!c) break;
            ++accepted;
            clients.push_back(Client{std::move(c), false});
        }
        for (Client& c : clients) {
            if (c.answered || !c.conn) continue;
            std::vector<uint8_t> msg;
            if (!c.conn->poll(msg)) continue;
            net::HelloMsg hello;
            if (net::peek_type(msg) == net::MsgType::Hello && net::decode(msg, hello)) hellos.push_back(hello);
            c.answered = true;
            if (mode == Mode::Reject) {
                c.conn->send(net::encode(net::RejectMsg{reason}));
                c.conn->close();
            } else if (mode == Mode::HangUp) {
                c.conn->close();
            }
        }
    }
    // How many of the clients are still connected to it
    int connected() {
        int n = 0;
        std::vector<uint8_t> nothing;
        for (Client& c : clients) {
            if (c.conn) c.conn->poll(nothing);
            if (c.conn && c.conn->state() == net::Connection::State::Open) ++n;
        }
        return n;
    }
};

// The application, bare machines and servers, stepped together in 10 ms of game time (`real_time`: a server in another process needs real time to answer)
struct Hall {
    Server* server{nullptr};
    Application* app{nullptr};
    std::vector<Peer*> peers;
    RawServer* raw{nullptr};
    bool real_time{false};

    void step(uint32_t ms) {
        for (uint32_t t = 0; t < ms; t += 10) {
            if (app != nullptr) {
                app->pump_network(0.010f);
                app->update_simulation(0.010f);
            }
            for (Peer* p : peers) {
                p->now += 10;
                p->update();
            }
            if (server != nullptr) {
                server->now += 10;
                server->update();
            }
            if (raw != nullptr) raw->update();
            if (real_time) std::this_thread::sleep_for(std::chrono::milliseconds(10));
            else ants_test::short_pause();
        }
    }
    bool until(const std::function<bool()>& cond, uint32_t max_ms) {
        for (uint32_t t = 0; t < max_ms; t += 10) {
            if (cond()) return true;
            step(10);
        }
        // the game clock is virtual but the sockets are real: on a busy machine the kernel can be late, so it gets up to two more seconds of real time with the game
        // clock standing still (nothing times out meanwhile); a wait that succeeds never gets here
        for (int i = 0; i < 2000 && !cond(); ++i) {
            if (app != nullptr) app->pump_network(0.0f);
            for (Peer* p : peers) p->update();
            if (server != nullptr) server->update();
            if (raw != nullptr) raw->update();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return cond();
    }
    // Both machines stand at the same tick at some moment: their states are then equal
    bool identical(const sim::SimulationEngine& a, const sim::SimulationEngine& b) {
        for (int i = 0; i < 400; ++i) {
            if (a.current_tick() == b.current_tick()) return a.state_hash() == b.state_hash();
            step(10);
        }
        return false;
    }
};

// ---- the menu, driven the way the window drives it -------------------------------------------------------------------------------------------------------------------

inline ApplicationConfig menu_config(const std::string& server_address, const std::string& settings_path) {
    ApplicationConfig cfg;
    cfg.headless = true;
    cfg.start_in_map_select = true;
    cfg.lan_port = 0;                                      // the tests do not announce on the real network
    cfg.start_menu = true;
    cfg.settings_path = settings_path;
    cfg.server = server_address;
    return cfg;
}

// A settings file that says: no quick help at the start (the screens that follow the menu are then the setup screen at once)
inline void write_no_quick_help(const std::string& path) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << "Show Quick Help at Startup=0\n";
}

// A settings file of this name with no quick help at the start (and the path of it)
inline std::string ini(const TempDir& temp, const std::string& name) {
    write_no_quick_help(temp.file(name));
    return temp.file(name);
}

inline SDL_Event key_event(SDL_Keycode sym, uint16_t mod = 0, bool repeat = false) {
    SDL_Event e;
    std::memset(&e, 0, sizeof(e));
    e.type = SDL_KEYDOWN;
    e.key.keysym.sym = sym;
    e.key.keysym.mod = mod;
    e.key.repeat = repeat ? 1 : 0;
    return e;
}
inline SDL_Event text_event(const std::string& text) {
    SDL_Event e;
    std::memset(&e, 0, sizeof(e));
    e.type = SDL_TEXTINPUT;
    std::snprintf(e.text.text, sizeof(e.text.text), "%s", text.c_str());
    return e;
}
inline SDL_Event mouse_event(uint32_t type, int32_t x, int32_t y, uint8_t button = SDL_BUTTON_LEFT) {
    SDL_Event e;
    std::memset(&e, 0, sizeof(e));
    e.type = type;
    if (type == SDL_MOUSEMOTION) {
        e.motion.x = x;
        e.motion.y = y;
    } else {
        e.button.x = x;
        e.button.y = y;
        e.button.button = button;
        e.button.clicks = 1;
    }
    return e;
}

// A key as a person presses it: the panel has been up for longer than StartMenu::kSettleMs by then (Enter and Space ignore a press within it, Esc does not quit from the first
// panel); `quick_press` is a key that comes at once (the tests of that rule)
inline void quick_press(Application& app, SDL_Keycode sym, uint16_t mod = 0) { app.handle_menu_event(key_event(sym, mod)); }
inline void press(Application& app, SDL_Keycode sym, uint16_t mod = 0) {
    if (sym == SDLK_RETURN || sym == SDLK_KP_ENTER || sym == SDLK_SPACE || sym == SDLK_ESCAPE) app.start_menu().update(static_cast<float>(StartMenu::kSettleMs + 10) / 1000.0f);
    quick_press(app, sym, mod);
}
inline void type_text(Application& app, const std::string& text) { app.handle_menu_event(text_event(text)); }

// A click on a control by the mouse: motion, press, release at its middle
inline bool click(Application& app, MenuId id) {
    MenuElement e;
    if (!app.start_menu().find_element(id, e)) return false;
    const int32_t x = e.rect.x + e.rect.w / 2;
    const int32_t y = e.rect.y + e.rect.h / 2;
    app.handle_menu_event(mouse_event(SDL_MOUSEMOTION, x, y));
    app.handle_menu_event(mouse_event(SDL_MOUSEBUTTONDOWN, x, y));
    app.handle_menu_event(mouse_event(SDL_MOUSEBUTTONUP, x, y));
    return true;
}

// The events of one click as the window queues them (SDL's count of the click in every one: 1 for a first click, 2 for the second of a double click), queued like the window's own
// (SDL stamps them with the time of the queueing); the application takes them in its next frame
struct Pt {
    int32_t x{0};
    int32_t y{0};
};
inline void queue_event(SDL_Event e) { SDL_PushEvent(&e); }
inline void queue_press(Pt at, uint8_t clicks) {
    SDL_Event e = mouse_event(SDL_MOUSEBUTTONDOWN, at.x, at.y);
    e.button.clicks = clicks;
    queue_event(e);
}
inline void queue_release(Pt at, uint8_t clicks) {
    SDL_Event e = mouse_event(SDL_MOUSEBUTTONUP, at.x, at.y);
    e.button.clicks = clicks;
    queue_event(e);
}
inline void queue_click(Pt at, uint8_t clicks) {
    queue_event(mouse_event(SDL_MOUSEMOTION, at.x, at.y));
    queue_press(at, clicks);
    queue_release(at, clicks);
}
inline Pt middle_of(Application& app, MenuId id) {
    MenuElement e;
    app.start_menu().find_element(id, e);
    return Pt{e.rect.x + e.rect.w / 2, e.rect.y + e.rect.h / 2};
}
inline bool lies_on(Application& app, MenuId id, Pt at) {
    MenuElement e;
    return app.start_menu().find_element(id, e) && e.rect.contains(at.x, at.y);
}

// Fills a text field as a player does: a click puts the focus on it, Ctrl+A selects what is there, the typing replaces it
inline void fill(Application& app, MenuId field, const std::string& text) {
    click(app, field);
    press(app, SDLK_a, KMOD_CTRL);
    type_text(app, text);
}

// "Join with a code" from the first panel: the name (when given), the code, the Join button
inline void menu_join(Application& app, const std::string& name, const std::string& code) {
    click(app, MenuId::JoinWithCode);
    if (!name.empty()) fill(app, MenuId::Name, name);
    fill(app, MenuId::Code, code);
    click(app, MenuId::Join);
}

// "Host an online match" from the first panel: the map (a click goes forward through the six; the panel opens on Treasure, the default, so the first two clicks go round to Tiny, the first of the
// six, and `map_clicks` counts from Tiny as it always did in these tests), the players (the page's default is 4: a click gives 2, two clicks 3), the name, Host
inline void menu_host(Application& app, const std::string& name, int map_clicks, int players_clicks) {
    click(app, MenuId::HostOnline);
    for (int i = 0; i < 2; ++i) click(app, MenuId::HostMap);                         // Treasure (the default) -> Islands -> Tiny
    for (int i = 0; i < map_clicks; ++i) click(app, MenuId::HostMap);
    for (int i = 0; i < players_clicks; ++i) click(app, MenuId::HostPlayers);
    if (!name.empty()) fill(app, MenuId::HostName, name);
    click(app, MenuId::Host);
}

inline bool on_panel(Application& app, MenuPanel panel) { return app.state() == AppState::StartMenu && app.start_menu().panel() == panel; }
inline bool failed_on(Application& app, MenuPanel panel) { return on_panel(app, panel) && !app.start_menu().message().empty(); }

// The room's code of the panel that shows it
inline std::string shown_code(Application& app) { return app.start_menu().room_code(); }

// The levels of the seats after the leader's (Red, Blue, Black) as the Host panel's rows ask for them: the plan of a request (the leader's seat gets nothing)
inline net::FillPlan fill_of(net::FillLevel red, net::FillLevel blue, net::FillLevel black) {
    net::FillPlan plan;
    plan.level[1] = red;
    plan.level[2] = blue;
    plan.level[3] = black;
    return plan;
}

// The quick help (when the option shows it) is closed the way a player closes it
inline void to_setup_screen(Application& app) {
    if (app.state() == AppState::QuickHelp) app.quick_help_key(SDLK_RETURN);
}

inline void leave_window(Application& app) {
    SDL_WindowEvent we;
    std::memset(&we, 0, sizeof(we));
    we.event = SDL_WINDOWEVENT_LEAVE;
    app.handle_window_event(we);
}

// The screenshot of what the window shows now
inline void shot(Application& app, const std::string& path) {
    leave_window(app);                                                  // (the pointer is outside, so that the game's cursor is not drawn on it)
    app.renderer().request_screenshot(path);
    app.render_frame();
}

// (the button's own rectangle: the setup screen of the 16:9 picture, which a command line gets by default, has the buttons elsewhere than the original's 640 x 480 page)
inline void click_fog(Application& app, bool on) {
    const ButtonRect& rect = (on ? app.map_select().fog_on_button() : app.map_select().fog_off_button()).up_rect();
    const int32_t x = rect.x + 2;
    const int32_t y = rect.y + 2;
    app.map_select().handle_mouse_motion(x, y);
    app.map_select().handle_mouse_down(x, y, 1);
    app.map_select().handle_mouse_up(x, y, 1);
}

inline char** argv_of(std::vector<std::string>& args, std::vector<char*>& storage) {
    storage.clear();
    for (auto& a : args) storage.push_back(a.data());
    storage.push_back(nullptr);
    return storage.data();
}

// What a match looks like when it has just started and has run for the 5 s of the "Get ready" dialog (100 steps of 50 ms, in which the simulation waits: the match clock shows its full time) and
// 100 ticks more (the bots look from the first tick on): what two ways into the same game must agree on
struct Played {
    uint8_t roster{0};
    uint64_t hash{0};
    size_t ants{0};
    bool bots{false};
    uint8_t bot_seats{0};
    bool fog{false};
    std::array<std::string, 4> names{};
    uint8_t seat{0};
    uint32_t decisions1{0};
    uint32_t decisions3{0};
    bool dialog_up_during_99_steps{false};     // the "Get ready" dialog was up after each of the steps 1 .. 99 (50 ms of real time each), and gone after the 100th
    uint64_t ticks_in_the_dialog{0};           // the simulation's ticks during those steps: none, the match clock waits for the dialog
    uint32_t looks_in_the_dialog{0};           // the looks of the bots during those steps: none (no tick, no look)
    uint32_t orders_in_the_dialog{0};          // and the orders they released
    uint64_t ticks_after{0};                   // the simulation's ticks after the 100 steps and 100 more: 100
    AppState state{AppState::MapSelect};
};

inline Played snapshot(Application& app) {
    Played p;
    p.state = app.state();
    p.roster = app.sim().roster_mask();
    p.ants = app.sim().get_world_state().ants.size();
    p.bots = app.bots() != nullptr;
    p.bot_seats = p.bots ? app.bots()->seat_mask() : uint8_t{0};
    p.fog = app.sim().is_fog_of_war_enabled();
    for (uint8_t s = 0; s < 4; ++s) p.names[s] = app.sim().get_player_name(s);
    p.seat = app.local_player_id();
    p.dialog_up_during_99_steps = app.hud().is_match_start_modal_active();
    for (uint32_t i = 1; i <= HUD::kMatchStartModalSteps + 100u; ++i) {
        app.update_simulation(0.05f);
        if (i < HUD::kMatchStartModalSteps) {
            p.dialog_up_during_99_steps = p.dialog_up_during_99_steps && app.hud().is_match_start_modal_active();
            p.ticks_in_the_dialog += app.sim().current_tick();
            if (p.bots) {
                p.looks_in_the_dialog += app.bots()->stats(1).decisions + app.bots()->stats(3).decisions;
                p.orders_in_the_dialog += app.bots()->stats(1).released + app.bots()->stats(3).released;
            }
        } else if (i == HUD::kMatchStartModalSteps) {
            p.dialog_up_during_99_steps = p.dialog_up_during_99_steps && !app.hud().is_match_start_modal_active();
            p.ticks_in_the_dialog += app.sim().current_tick();
        }
    }
    p.ticks_after = app.sim().current_tick();
    p.hash = app.sim().state_hash().total;
    if (p.bots) {
        p.decisions1 = app.bots()->stats(1).decisions;
        p.decisions3 = app.bots()->stats(3).decisions;
    }
    return p;
}

inline bool same(const Played& a, const Played& b) {
    return a.roster == b.roster && a.hash == b.hash && a.ants == b.ants && a.bots == b.bots && a.bot_seats == b.bot_seats && a.fog == b.fog && a.names == b.names && a.seat == b.seat &&
           a.decisions1 == b.decisions1 && a.decisions3 == b.decisions3 && a.state == b.state && a.dialog_up_during_99_steps == b.dialog_up_during_99_steps &&
           a.ticks_in_the_dialog == b.ticks_in_the_dialog && a.looks_in_the_dialog == b.looks_in_the_dialog && a.orders_in_the_dialog == b.orders_in_the_dialog && a.ticks_after == b.ticks_after;
}


// ---- a scenario that needs a server and nothing else (the in-process one, or a real one of another process) -----------------------------------------------------------------

// The application hosts a room through the menu, a bare machine joins with the code, the application's leader presses START, both play: false with a line that says what failed
inline bool host_join_start(Hall& hall, Application& app, Peer& peer, const std::string& host, uint16_t port, std::string& report) {
    menu_host(app, "Hostess", 0, 2);                                    // Tiny, three players (two of them play: the room is not full, the leader starts it)
    if (!hall.until([&]() { return on_panel(app, MenuPanel::Room) || failed_on(app, MenuPanel::Host); }, 20000)) {
        report = "the room's panel did not come";
        return false;
    }
    if (!on_panel(app, MenuPanel::Room)) {
        report = "the host attempt failed: " + app.start_menu().message();
        return false;
    }
    const std::string code = shown_code(app);
    report = "room " + code;
    if (!peer.join(host, port, "Guest", code)) {
        report += ": the second client could not connect";
        return false;
    }
    hall.peers = {&peer};
    if (!hall.until([&]() { return peer.net.phase() == net::NetGame::Phase::Room && app.net() != nullptr && app.net()->room().slots[1].state == net::SlotState::Client; }, 20000)) {
        report += ": the second client did not get into the room";
        return false;
    }
    click(app, MenuId::EnterRoom);
    hall.step(20);
    to_setup_screen(app);
    hall.step(600);
    if (app.state() != AppState::MapSelect || !app.map_select().leads_server_room()) {
        report += ": the leader's screen did not come";
        return false;
    }
    app.map_select().handle_key_down(SDLK_RETURN);                       // START
    if (!hall.until([&]() { return app.state() == AppState::Playing && peer.net.phase() == net::NetGame::Phase::Playing; }, 30000)) {
        report += ": the match did not start";
        return false;
    }
    hall.step(3000);
    if (!hall.identical(app.sim(), peer.sim)) {
        report += ": the two machines differ";
        return false;
    }
    report += ": a two-player match in a room for three, started by the leader, both machines identical at tick " + std::to_string(app.sim().current_tick());
    return true;
}

}  // namespace

// The groups of cases, run by main() in test_start_menu_app.cpp in this order
void run_start_menu_app_play_tests();
void run_start_menu_app_options_tests();
