// The desktop start menu in the application (include/ants_app/start_menu.hpp, src/ants_app/application_menu.cpp): the whole of what a player does with it, against a REAL
// RoomManager of the dedicated server behind a real TCP listener on the loopback interface (as test_network_app does for the leader's START): single player with no bot (the
// original game exactly) and with bots (seated as --bot seats them, fog off), join a demo room, host a room (the code, the leader's screen, a second client, START), every way
// a join can fail, cancel, and the way back to the menu after a network game. One Application per test (SDL is initialised once per process).
//
//   test_start_menu_app                         runs the tests
//   test_start_menu_app --shots DIR             writes the screenshots of every panel and state of the menu as DIR/*.png (they are what the menu looks like; nothing is compared)
//   test_start_menu_app --real-server H:P       runs the host / join / START scenario against a game server that is already running (the gate "a real server": a native ants_server
//                                               on localhost started with --demo-rooms 4 --demo-map TINY.LVL ...), then exits
//   test_start_menu_app --real-fill H:P         hosts a room through the menu with "Empty seats at START" = Medium bots, starts it ALONE (the server seats three bots) and plays it
//                                               for ten seconds of real time (the rate of the ticks is measured and printed: 20 a second), against a game server that is already running
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

using namespace ants;
using namespace ants::app;

namespace fs = std::filesystem;

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

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

#define TEST_CASE(name) run_test_case(name, [&]()
#define TEST_END() );
#define ASSERT_TRUE(cond) \
    do { \
        ++g_assert_count; \
        if (!(cond)) { \
            std::cout << "FAILED!\n    Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_test_failures; \
            return; \
        } \
    } while (0)
#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))
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

std::string maps_dir() { return std::string(ORIGINAL_ASSETS_DIR) + "/Maps/"; }

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

std::string read_file(const std::string& path) {
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

HostLookup::Resolver lookup_of(const std::shared_ptr<Lookup>& state) {
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
                    sim.init(level, s.seed, s.roster);
                }
                net.report_loaded(ok);
            }
        }
    }
    bool join(const std::string& host, uint16_t port, const std::string& name, const std::string& room) { return net.join(host, port, name, 255, room); }
};

// ---- a dedicated server in the test: the real room manager behind a real TCP listener ----------------------------------------------------------------------------------

server::ServerLimits demo_limits(size_t demo_rooms, const std::vector<std::string>& maps) {
    server::ServerLimits limits;
    limits.demo_rooms = demo_rooms;
    limits.demo_map = "TINY.LVL";
    limits.demo_maps = maps;
    return limits;
}
const std::vector<std::string> kAllMaps = {"TINY.LVL", "SMALL.LVL", "MEDIUM.LVL", "GAUNTLET.LVL", "TREASURE.LVL", "ISLANDS.LVL"};

struct Server {
    server::RoomManager mgr;
    std::unique_ptr<net::TcpListener> listener{net::TcpListener::listen(0, true)};
    uint32_t now{1000};

    // `maps`: the maps that the server's demo rooms may be made on (--demo-maps; the menu offers the six of the original, a server may offer fewer)
    explicit Server(size_t demo_rooms = 4, const std::vector<std::string>& maps = kAllMaps) : mgr(server::MapStore(std::string(ORIGINAL_ASSETS_DIR) + "/Maps"), demo_limits(demo_rooms, maps)) {}
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
    // A room that is not a demo room (the control interface makes these): `players` seats on the map
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
            std::this_thread::sleep_for(real_time ? std::chrono::milliseconds(10) : std::chrono::microseconds(300));
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

ApplicationConfig menu_config(const std::string& server_address, const std::string& settings_path) {
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
void write_no_quick_help(const std::string& path) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << "Show Quick Help at Startup=0\n";
}

// A settings file of this name with no quick help at the start (and the path of it)
std::string ini(const TempDir& temp, const std::string& name) {
    write_no_quick_help(temp.file(name));
    return temp.file(name);
}

SDL_Event key_event(SDL_Keycode sym, uint16_t mod = 0, bool repeat = false) {
    SDL_Event e;
    std::memset(&e, 0, sizeof(e));
    e.type = SDL_KEYDOWN;
    e.key.keysym.sym = sym;
    e.key.keysym.mod = mod;
    e.key.repeat = repeat ? 1 : 0;
    return e;
}
SDL_Event text_event(const std::string& text) {
    SDL_Event e;
    std::memset(&e, 0, sizeof(e));
    e.type = SDL_TEXTINPUT;
    std::snprintf(e.text.text, sizeof(e.text.text), "%s", text.c_str());
    return e;
}
SDL_Event mouse_event(uint32_t type, int32_t x, int32_t y, uint8_t button = SDL_BUTTON_LEFT) {
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
void quick_press(Application& app, SDL_Keycode sym, uint16_t mod = 0) { app.handle_menu_event(key_event(sym, mod)); }
void press(Application& app, SDL_Keycode sym, uint16_t mod = 0) {
    if (sym == SDLK_RETURN || sym == SDLK_KP_ENTER || sym == SDLK_SPACE || sym == SDLK_ESCAPE) app.start_menu().update(static_cast<float>(StartMenu::kSettleMs + 10) / 1000.0f);
    quick_press(app, sym, mod);
}
void type_text(Application& app, const std::string& text) { app.handle_menu_event(text_event(text)); }

// A click on a control by the mouse: motion, press, release at its middle
bool click(Application& app, MenuId id) {
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
void queue_event(SDL_Event e) { SDL_PushEvent(&e); }
void queue_press(Pt at, uint8_t clicks) {
    SDL_Event e = mouse_event(SDL_MOUSEBUTTONDOWN, at.x, at.y);
    e.button.clicks = clicks;
    queue_event(e);
}
void queue_release(Pt at, uint8_t clicks) {
    SDL_Event e = mouse_event(SDL_MOUSEBUTTONUP, at.x, at.y);
    e.button.clicks = clicks;
    queue_event(e);
}
void queue_click(Pt at, uint8_t clicks) {
    queue_event(mouse_event(SDL_MOUSEMOTION, at.x, at.y));
    queue_press(at, clicks);
    queue_release(at, clicks);
}
Pt middle_of(Application& app, MenuId id) {
    MenuElement e;
    app.start_menu().find_element(id, e);
    return Pt{e.rect.x + e.rect.w / 2, e.rect.y + e.rect.h / 2};
}
bool lies_on(Application& app, MenuId id, Pt at) {
    MenuElement e;
    return app.start_menu().find_element(id, e) && e.rect.contains(at.x, at.y);
}

// Fills a text field as a player does: a click puts the focus on it, Ctrl+A selects what is there, the typing replaces it
void fill(Application& app, MenuId field, const std::string& text) {
    click(app, field);
    press(app, SDLK_a, KMOD_CTRL);
    type_text(app, text);
}

// "Join with a code" from the first panel: the name (when given), the code, the Join button
void menu_join(Application& app, const std::string& name, const std::string& code) {
    click(app, MenuId::JoinWithCode);
    if (!name.empty()) fill(app, MenuId::Name, name);
    fill(app, MenuId::Code, code);
    click(app, MenuId::Join);
}

// "Host an online match" from the first panel: the map (a click goes forward through the six; the panel opens on Treasure, the default, so the first two clicks go round to Tiny, the first of the
// six, and `map_clicks` counts from Tiny as it always did in these tests), the players (the page's default is 4: a click gives 2, two clicks 3), the name, Host
void menu_host(Application& app, const std::string& name, int map_clicks, int players_clicks) {
    click(app, MenuId::HostOnline);
    for (int i = 0; i < 2; ++i) click(app, MenuId::HostMap);                         // Treasure (the default) -> Islands -> Tiny
    for (int i = 0; i < map_clicks; ++i) click(app, MenuId::HostMap);
    for (int i = 0; i < players_clicks; ++i) click(app, MenuId::HostPlayers);
    if (!name.empty()) fill(app, MenuId::HostName, name);
    click(app, MenuId::Host);
}

bool on_panel(Application& app, MenuPanel panel) { return app.state() == AppState::StartMenu && app.start_menu().panel() == panel; }
bool failed_on(Application& app, MenuPanel panel) { return on_panel(app, panel) && !app.start_menu().message().empty(); }

// The room's code of the panel that shows it
std::string shown_code(Application& app) { return app.start_menu().room_code(); }

// The quick help (when the option shows it) is closed the way a player closes it
void to_setup_screen(Application& app) {
    if (app.state() == AppState::QuickHelp) app.quick_help_key(SDLK_RETURN);
}

void leave_window(Application& app) {
    SDL_WindowEvent we;
    std::memset(&we, 0, sizeof(we));
    we.event = SDL_WINDOWEVENT_LEAVE;
    app.handle_window_event(we);
}

// The screenshot of what the window shows now
void shot(Application& app, const std::string& path) {
    leave_window(app);                                                  // (the pointer is outside, so that the game's cursor is not drawn on it)
    app.renderer().request_screenshot(path);
    app.render_frame();
}

// (the button's own rectangle: the setup screen of the 16:9 picture, which a command line gets by default, has the buttons elsewhere than the original's 640 x 480 page)
void click_fog(Application& app, bool on) {
    const ButtonRect& rect = (on ? app.map_select().fog_on_button() : app.map_select().fog_off_button()).up_rect();
    const int32_t x = rect.x + 2;
    const int32_t y = rect.y + 2;
    app.map_select().handle_mouse_motion(x, y);
    app.map_select().handle_mouse_down(x, y, 1);
    app.map_select().handle_mouse_up(x, y, 1);
}

char** argv_of(std::vector<std::string>& args, std::vector<char*>& storage) {
    storage.clear();
    for (auto& a : args) storage.push_back(a.data());
    storage.push_back(nullptr);
    return storage.data();
}

// What a match looks like when it has just started and has run for the 100 ticks of the "Get ready" dialog and 100 more (the bots look from the end of the dialog on): what two ways
// into the same game must agree on
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
    bool dialog_up_during_99_ticks{false};     // the "Get ready" dialog was up after each of the ticks 1 .. 99, and gone after the 100th
    uint32_t looks_in_the_dialog{0};           // the looks of the bots during those ticks: none (the start hold)
    uint32_t orders_in_the_dialog{0};          // and the orders they released
    AppState state{AppState::MapSelect};
};

Played snapshot(Application& app) {
    Played p;
    p.state = app.state();
    p.roster = app.sim().roster_mask();
    p.ants = app.sim().get_world_state().ants.size();
    p.bots = app.bots() != nullptr;
    p.bot_seats = p.bots ? app.bots()->seat_mask() : uint8_t{0};
    p.fog = app.sim().is_fog_of_war_enabled();
    for (uint8_t s = 0; s < 4; ++s) p.names[s] = app.sim().get_player_name(s);
    p.seat = app.local_player_id();
    p.dialog_up_during_99_ticks = app.hud().is_match_start_modal_active();
    for (uint32_t i = 1; i <= 100u + sim::kMatchStartHoldTicks; ++i) {
        app.update_simulation(0.05f);
        if (i < sim::kMatchStartHoldTicks) {
            p.dialog_up_during_99_ticks = p.dialog_up_during_99_ticks && app.hud().is_match_start_modal_active();
            if (p.bots) {
                p.looks_in_the_dialog += app.bots()->stats(1).decisions + app.bots()->stats(3).decisions;
                p.orders_in_the_dialog += app.bots()->stats(1).released + app.bots()->stats(3).released;
            }
        } else if (i == sim::kMatchStartHoldTicks) {
            p.dialog_up_during_99_ticks = p.dialog_up_during_99_ticks && !app.hud().is_match_start_modal_active();
        }
    }
    p.hash = app.sim().state_hash().total;
    if (p.bots) {
        p.decisions1 = app.bots()->stats(1).decisions;
        p.decisions3 = app.bots()->stats(3).decisions;
    }
    return p;
}

bool same(const Played& a, const Played& b) {
    return a.roster == b.roster && a.hash == b.hash && a.ants == b.ants && a.bots == b.bots && a.bot_seats == b.bot_seats && a.fog == b.fog && a.names == b.names && a.seat == b.seat &&
           a.decisions1 == b.decisions1 && a.decisions3 == b.decisions3 && a.state == b.state && a.dialog_up_during_99_ticks == b.dialog_up_during_99_ticks &&
           a.looks_in_the_dialog == b.looks_in_the_dialog && a.orders_in_the_dialog == b.orders_in_the_dialog;
}

// ---- the screenshots ---------------------------------------------------------------------------------------------------------------------------------------------------

int make_shots(const std::string& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    TempDir temp;
    Server server(1);
    Application app;
    Hall hall{&server, &app, {}, nullptr, false};
    const auto slow = std::make_shared<Lookup>();                    // a name that is slow to resolve: the "Connecting" panel stays
    ApplicationConfig cfg = menu_config(server.address(), temp.file("settings.ini"));
    cfg.host_resolver = lookup_of(slow);
    cfg.clipboard_set = [](const std::string&) { return true; };
    if (!app.init(cfg)) return 1;
    app.start_menu().update(0.5f);
    shot(app, dir + "/01_first_panel.png");
    press(app, SDLK_DOWN);
    shot(app, dir + "/02_first_panel_selection.png");
    // single player: nobody, then two bots (red: medium, blue: hard) with the rule-8 line
    click(app, MenuId::Single);
    shot(app, dir + "/03_single_player_empty.png");
    click(app, MenuId::Seat1);
    click(app, MenuId::Seat1);
    click(app, MenuId::Seat2);
    click(app, MenuId::Seat2);
    click(app, MenuId::Seat2);
    shot(app, dir + "/04_single_player_two_bots.png");
    press(app, SDLK_ESCAPE);
    // join with a code: empty, typed, the name refused, connecting (a slow lookup), no such room, the server cannot be reached
    click(app, MenuId::JoinWithCode);
    shot(app, dir + "/05_join_empty.png");
    fill(app, MenuId::Code, "zz-no-such-room");
    shot(app, dir + "/06_join_typed.png");
    fill(app, MenuId::Name, "Bot (Hard)");
    click(app, MenuId::Join);
    shot(app, dir + "/07_join_name_refused.png");
    fill(app, MenuId::Name, "Dave");
    app.start_menu().set_server(ServerAddress{"localhost", server.port()});         // (a name, not an address: the lookup goes to the slow resolver above)
    click(app, MenuId::Join);
    hall.step(50);
    shot(app, dir + "/08_connecting.png");
    slow->release = true;
    if (!hall.until([&]() { return failed_on(app, MenuPanel::Join); }, 8000)) return 1;
    shot(app, dir + "/09_join_no_such_room.png");
    {
        const uint16_t closed = []() {
            auto l = net::TcpListener::listen(0, true);
            return l ? l->port() : uint16_t{1};
        }();
        app.start_menu().set_server(ServerAddress{"127.0.0.1", closed});
        click(app, MenuId::Join);
        if (!hall.until([&]() { return failed_on(app, MenuPanel::Join) && app.start_menu().message().find("Cannot reach") != std::string::npos; }, 8000)) return 1;
        shot(app, dir + "/10_join_server_unreachable.png");
        app.start_menu().set_server(ServerAddress{"127.0.0.1", server.port()});
    }
    // host an online match: the panel, then the room's code, copied
    press(app, SDLK_ESCAPE);
    click(app, MenuId::HostOnline);
    click(app, MenuId::HostMap);
    click(app, MenuId::HostPlayers);
    click(app, MenuId::HostPlayers);
    fill(app, MenuId::HostName, "Dave");
    shot(app, dir + "/11_host_panel.png");
    click(app, MenuId::Host);
    if (!hall.until([&]() { return on_panel(app, MenuPanel::Room); }, 8000)) return 1;
    shot(app, dir + "/12_host_room_code.png");
    click(app, MenuId::Copy);
    shot(app, dir + "/13_host_room_code_copied.png");
    // the room is left; the one demo room of this server is still there, so the next host attempt is refused: the server is busy
    press(app, SDLK_ESCAPE);
    hall.step(50);
    click(app, MenuId::Host);
    if (!hall.until([&]() { return failed_on(app, MenuPanel::Host); }, 8000)) return 1;
    shot(app, dir + "/14_host_server_busy.png");
    press(app, SDLK_ESCAPE);
    app.start_menu().show_main("The connection to the other players was lost.");
    shot(app, dir + "/15_first_panel_notice.png");
    // what the review fixes added: a character that the fields cannot take, the copy that failed, a server that does not offer the map, the first panel after a lost game with the old code
    click(app, MenuId::JoinWithCode);
    press(app, SDLK_a, KMOD_CTRL);
    type_text(app, "caf\xC3\xA9-room");
    shot(app, dir + "/16_join_refused_character.png");
    press(app, SDLK_ESCAPE);
    click(app, MenuId::JoinWithCode);
    shot(app, dir + "/17_join_old_code_selected.png");
    press(app, SDLK_ESCAPE);
    app.start_menu().set_clipboard([]() { return std::string(); }, [](const std::string&) { return false; });
    app.start_menu().show_room("demo-gauntlet-4p-k3n7pq", 2, 4);
    click(app, MenuId::Copy);
    shot(app, dir + "/18_room_copy_failed.png");
    app.start_menu().show_main();
    Server small(2, {"TINY.LVL", "SMALL.LVL"});
    Hall hall2{&small, &app, {}, nullptr, false};
    app.start_menu().set_server(ServerAddress{"127.0.0.1", small.port()});
    click(app, MenuId::HostOnline);
    while (app.start_menu().settings().host_map != 5) click(app, MenuId::HostMap);                         // Islands
    click(app, MenuId::Host);
    if (!hall2.until([&]() { return failed_on(app, MenuPanel::Host); }, 8000)) {
        std::cerr << "the Host panel's line did not come: state " << static_cast<int>(app.state()) << " panel " << static_cast<int>(app.start_menu().panel()) << " message '" << app.start_menu().message() << "'\n";
        return 1;
    }
    shot(app, dir + "/19_host_map_not_offered.png");
    return 0;
}

// ---- a scenario that needs a server and nothing else (the in-process one, or a real one of another process) -----------------------------------------------------------------

// The application hosts a room through the menu, a bare machine joins with the code, the application's leader presses START, both play: false with a line that says what failed
bool host_join_start(Hall& hall, Application& app, Peer& peer, const std::string& host, uint16_t port, std::string& report) {
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

int run_real_server(const std::string& address) {
    ServerAddress server;
    std::string why;
    if (!parse_server(address, server, why)) {
        std::cerr << why << "\n";
        return 2;
    }
    TempDir temp;
    write_no_quick_help(temp.file("settings.ini"));
    Application app;
    Peer peer;
    Hall hall{nullptr, &app, {}, nullptr, true};
    if (!app.init(menu_config(address, temp.file("settings.ini")))) return 1;
    std::string report;
    const bool ok = host_join_start(hall, app, peer, server.host, server.port, report);
    std::cout << (ok ? "REAL SERVER OK: " : "REAL SERVER FAILED: ") << report << "\n";
    return ok ? 0 : 1;
}

// The menu's Host panel with "Empty seats at START" = Medium bots against a server that is already running (the real check of the fill: a server of a docker image, on the network of
// the machine): one person, alone, starts a room for four; the server seats three "Bot (Medium)" and the match runs at 20 ticks a second
int run_real_fill(const std::string& address) {
    ServerAddress server;
    std::string why;
    if (!parse_server(address, server, why)) {
        std::cerr << why << "\n";
        return 2;
    }
    TempDir temp;
    write_no_quick_help(temp.file("settings.ini"));
    Application app;
    Hall hall{nullptr, &app, {}, nullptr, true};
    if (!app.init(menu_config(address, temp.file("settings.ini")))) return 1;
    const auto fail = [](const std::string& what) {
        std::cout << "REAL FILL FAILED: " << what << "\n";
        return 1;
    };
    click(app, MenuId::HostOnline);
    click(app, MenuId::HostFill);                                                      // Leave empty -> Easy bots
    click(app, MenuId::HostFill);                                                      // -> Medium bots
    fill(app, MenuId::HostName, "Solo");
    click(app, MenuId::Host);
    if (!hall.until([&]() { return on_panel(app, MenuPanel::Room) || failed_on(app, MenuPanel::Host); }, 20000)) return fail("the room's panel did not come");
    if (!on_panel(app, MenuPanel::Room)) return fail("the host attempt failed: " + app.start_menu().message());
    const std::string code = shown_code(app);
    bool says = false;
    for (const MenuElement& e : app.start_menu().elements()) says = says || e.text == "Empty seats will be Medium bots.";
    if (!says) return fail("the room's panel does not say what START will do");
    click(app, MenuId::EnterRoom);
    hall.step(20);
    to_setup_screen(app);
    hall.step(700);
    if (app.state() != AppState::MapSelect || !app.map_select().leads_server_room()) return fail("the leader's screen did not come");
    if (app.map_select().room().status != "Press START: the empty seats get Medium bots.") return fail("the status line says \"" + app.map_select().room().status + "\"");
    app.room_key_down(SDLK_s, 0, false);                                               // START, alone
    if (!hall.until([&]() { return app.state() == AppState::Playing; }, 30000)) return fail("the match did not start (the status line: \"" + app.map_select().room().status + "\")");
    if (app.sim().roster_mask() != 0x0F) return fail("the roster is not all four seats");
    for (uint8_t seat = 1; seat < 4; ++seat) {
        if (app.sim().get_player_name(seat) != "Bot (Medium)") return fail("seat " + std::to_string(seat) + " is called \"" + app.sim().get_player_name(seat) + "\"");
    }
    hall.step(1000);                                                                   // (the first second: the match settles)
    const uint64_t t0 = app.sim().current_tick();
    const auto begin = std::chrono::steady_clock::now();
    hall.step(10000);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    const double rate = static_cast<double>(app.sim().current_tick() - t0) / seconds;
    if (app.net()->desynced()) return fail("the match is out of sync");
    std::cout << "REAL FILL " << (rate >= 17.0 && rate <= 23.0 ? "OK" : "FAILED") << ": room " << code << ", alone with three Bot (Medium), " << (app.sim().current_tick() - t0) << " ticks in "
              << seconds << " s = " << rate << " ticks a second, no desync\n";
    return rate >= 17.0 && rate <= 23.0 ? 0 : 1;
}

int run_probe(const std::string& address) {
    TempDir temp;
    Application app;
    Hall hall{nullptr, &app, {}, nullptr, true};
    ApplicationConfig cfg = menu_config(address, temp.file("settings.ini"));
    cfg.menu_connect_timeout_ms = 30000;
    if (!app.init(cfg)) return 1;
    const auto begin = std::chrono::steady_clock::now();
    menu_join(app, "Probe", "zz-no-such-room-probe");                   // (not a demo-... code: nothing is made on the server)
    const bool answered = hall.until([&]() { return failed_on(app, MenuPanel::Join); }, 40000);
    const long ms = static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin).count());
    std::cout << "PROBE " << address << ": " << (answered ? "answered" : "NO ANSWER") << " after " << ms << " ms: \"" << app.start_menu().message() << "\"\n";
    return answered ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--shots") == 0) return make_shots(argv[i + 1]);
        if (std::strcmp(argv[i], "--real-server") == 0) return run_real_server(argv[i + 1]);
        if (std::strcmp(argv[i], "--real-fill") == 0) return run_real_fill(argv[i + 1]);
        if (std::strcmp(argv[i], "--probe") == 0) return run_probe(argv[i + 1]);
    }
    std::cout << "=== Start menu in the application ===\n";

    TEST_CASE("A1.1 Start: a native game with the menu starts at the menu (not at the setup screen), the game under it does not run, the loading screen's end leads to it, and a config without the menu starts as it always did") {
        TempDir temp;
        Server server;
        {
            Application app;
            ASSERT_TRUE(app.init(menu_config(server.address(), temp.file("settings.ini"))));
            ASSERT_TRUE(app.start_menu_enabled());
            ASSERT_EQ(app.state(), AppState::StartMenu);
            ASSERT_EQ(app.start_menu().panel(), MenuPanel::Main);
            ASSERT_TRUE(app.net() == nullptr && app.bots() == nullptr);
            const uint64_t tick = app.sim().current_tick();
            for (int i = 0; i < 200; ++i) app.update_simulation(0.05f);                      // 10 s of game time: nothing runs under the menu
            ASSERT_EQ(app.sim().current_tick(), tick);
            ASSERT_EQ(app.state(), AppState::StartMenu);
            app.render_frame();                                                              // the frame is drawn
            // the loading screen's end (a key, a click, the 30 ticks) leads to the menu, never to the quick help or the setup screen
            app.finish_loading();
            ASSERT_EQ(app.state(), AppState::StartMenu);
            // Quit asks the program to end
            ASSERT_TRUE(app.is_running());
            click(app, MenuId::Quit);
            app.pump_network(0.01f);
            ASSERT_FALSE(app.is_running());
        }
        {
            Application app;
            ApplicationConfig cfg = menu_config(server.address(), temp.file("settings2.ini"));
            cfg.start_menu = false;                                                          // what a config made by hand has, and what every mode flag gives
            ASSERT_TRUE(app.init(cfg));
            ASSERT_FALSE(app.start_menu_enabled());
            ASSERT_EQ(app.state(), AppState::MapSelect);
            app.finish_loading();                                                            // (the loading screen's end of a game without a menu: the quick help)
            ASSERT_EQ(app.state(), AppState::QuickHelp);
        }
        {   // Esc on the first panel is Quit, and asks nothing
            Application app;
            ASSERT_TRUE(app.init(menu_config(server.address(), temp.file("settings3.ini"))));
            press(app, SDLK_ESCAPE);
            app.pump_network(0.01f);
            ASSERT_FALSE(app.is_running());
        }
    } TEST_END();

    TEST_CASE("A1.2 Settings: the name, the bots and the host's map and players survive a restart (two Application instances on one settings file); the server key is read, never written, and --server beats it; --name beats the stored name; a bad stored server is the default") {
        TempDir temp;
        const std::string settings = temp.file("settings.ini");
        {
            Application app;
            ASSERT_TRUE(app.init(menu_config("", settings)));
            ASSERT_TRUE(app.start_menu().server() == ServerAddress{});                       // nothing is said: the public server
            ASSERT_EQ(app.start_menu().name(), std::string("Player"));                       // the game's name for a network game
            click(app, MenuId::JoinWithCode);
            fill(app, MenuId::Name, "Maya");
            press(app, SDLK_ESCAPE);
            click(app, MenuId::Single);
            click(app, MenuId::Seat1);                                                       // red: easy
            click(app, MenuId::Seat3);
            click(app, MenuId::Seat3);
            click(app, MenuId::Seat3);                                                       // black: hard
            press(app, SDLK_ESCAPE);
            click(app, MenuId::HostOnline);
            click(app, MenuId::HostMap);                                                     // Treasure (the default) -> Islands
            click(app, MenuId::HostMap);                                                     // -> Tiny
            click(app, MenuId::HostMap);                                                     // -> small
            click(app, MenuId::HostPlayers);                                                 // 2
            click(app, MenuId::HostPlayers);                                                 // 3
        }
        const std::string text = read_file(settings);
        ASSERT_HAS(text, "name=Maya\n");
        ASSERT_HAS(text, "bots=off,easy,off,hard\n");
        ASSERT_HAS(text, "host_map=small\n");
        ASSERT_HAS(text, "host_players=3\n");
        ASSERT_TRUE(text.find("server=") == std::string::npos);                              // the menu never writes the server
        {   // the file gets a server by hand (and an option of the original beside it)
            std::ofstream out(settings, std::ios::app | std::ios::binary);
            out << "server=play.example.org:4010\nSound Volume=30\n";
        }
        {
            Application app;
            ASSERT_TRUE(app.init(menu_config("", settings)));
            ASSERT_EQ(app.start_menu().name(), std::string("Maya"));
            ASSERT_TRUE(app.start_menu().seat(0) == SeatChoice::Empty && app.start_menu().seat(1) == SeatChoice::Easy && app.start_menu().seat(2) == SeatChoice::Empty &&
                        app.start_menu().seat(3) == SeatChoice::Hard);
            ASSERT_TRUE(app.start_menu().server() == (ServerAddress{"play.example.org", 4010}));    // the stored server
            click(app, MenuId::Single);
            MenuElement e;
            ASSERT_TRUE(app.start_menu().find_element(MenuId::Seat1, e) && e.value == "Easy bot");
            ASSERT_TRUE(app.start_menu().find_element(MenuId::Seat3, e) && e.value == "Hard bot");
            press(app, SDLK_ESCAPE);
            click(app, MenuId::HostOnline);
            ASSERT_TRUE(app.start_menu().find_element(MenuId::HostMap, e) && e.value == "Small");
            ASSERT_TRUE(app.start_menu().find_element(MenuId::HostPlayers, e) && e.value == "3 players");
        }
        {   // --server beats the stored one; --name beats the stored name; neither is written back
            Application app;
            ApplicationConfig cfg = menu_config("other.example.org:5000", settings);
            cfg.player_name = "Zed";
            ASSERT_TRUE(app.init(cfg));
            ASSERT_TRUE(app.start_menu().server() == (ServerAddress{"other.example.org", 5000}));
            ASSERT_EQ(app.start_menu().name(), std::string("Zed"));
        }
        const std::string after = read_file(settings);
        ASSERT_HAS(after, "name=Maya\n");
        ASSERT_HAS(after, "server=play.example.org:4010\n");
        ASSERT_HAS(after, "Sound Volume=30\n");
        {   // a stored server that is no server is the default (with a message on stderr), never a crash
            std::ofstream out(settings, std::ios::app | std::ios::binary);
            out << "server=not a server!\n";
        }
        {
            Application app;
            ASSERT_TRUE(app.init(menu_config("", settings)));
            ASSERT_TRUE(app.start_menu().server() == ServerAddress{});
        }
        // the server of the command line must parse: a bad one never gets to the menu (parse_arguments refuses it, init refuses the config)
        std::vector<std::string> args = {"ants", "--headless", "--start-menu", "--server", "no good", "--settings", settings};
        std::vector<char*> storage;
        const ApplicationConfig bad = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, storage));
        ASSERT_HAS(bad.startup_error, "--server no good");
        Application refused;
        ASSERT_FALSE(refused.init(bad));
    } TEST_END();

    TEST_CASE("A2.1 Single player with nobody: the match is the original's single-player game exactly: the same four teams, no bot code, the same state at tick 200 (the 100 ticks of the get-ready dialog and 100 more) as the game that never saw the menu") {
        TempDir temp;
        Server server;
        write_no_quick_help(temp.file("a.ini"));
        write_no_quick_help(temp.file("b.ini"));
        Played via_menu;
        {
            Application app;
            ASSERT_TRUE(app.init(menu_config(server.address(), temp.file("a.ini"))));
            click(app, MenuId::Single);
            ASSERT_EQ(app.start_menu().panel(), MenuPanel::Single);
            click(app, MenuId::Continue);                                                    // nobody: Continue
            app.pump_network(0.01f);
            ASSERT_EQ(app.state(), AppState::MapSelect);                                     // (the quick help is off in this file)
            ASSERT_TRUE(app.bots() == nullptr);
            app.map_select().handle_key_down(SDLK_RETURN);                                   // START on the setup screen
            ASSERT_EQ(app.state(), AppState::Playing);
            ASSERT_TRUE(app.bots() == nullptr);
            via_menu = snapshot(app);
        }
        Played plain;
        {
            Application app;
            ApplicationConfig cfg = menu_config(server.address(), temp.file("b.ini"));
            cfg.start_menu = false;
            ASSERT_TRUE(app.init(cfg));
            ASSERT_EQ(app.state(), AppState::MapSelect);
            app.map_select().handle_key_down(SDLK_RETURN);
            ASSERT_EQ(app.state(), AppState::Playing);
            plain = snapshot(app);
        }
        ASSERT_EQ(via_menu.roster, 0x0F);
        ASSERT_FALSE(via_menu.bots);
        ASSERT_TRUE(same(via_menu, plain));
        // with the quick help on (the default), it comes between: Continue, the quick help, the setup screen
        {
            Application app;
            ASSERT_TRUE(app.init(menu_config(server.address(), temp.file("c.ini"))));
            click(app, MenuId::Single);
            click(app, MenuId::Continue);
            app.pump_network(0.01f);
            ASSERT_EQ(app.state(), AppState::QuickHelp);
            app.quick_help_key(SDLK_RETURN);
            ASSERT_EQ(app.state(), AppState::MapSelect);
        }
    } TEST_END();

    TEST_CASE("A2.2 Single player with two bots: the match starts with exactly the bots that --bot 1:medium --bot 3:hard gives (same roster, names, bot seats, state at tick 200, decisions); the fog option refuses START with the reason, without fog it starts") {
        TempDir temp;
        Server server;
        write_no_quick_help(temp.file("a.ini"));
        write_no_quick_help(temp.file("b.ini"));
        Played via_menu;
        std::string refusal;
        {
            Application app;
            ASSERT_TRUE(app.init(menu_config(server.address(), temp.file("a.ini"))));
            click(app, MenuId::Single);
            click(app, MenuId::Seat1);
            click(app, MenuId::Seat1);                                                       // red: medium
            click(app, MenuId::Seat3);
            click(app, MenuId::Seat3);
            click(app, MenuId::Seat3);                                                       // black: hard
            ASSERT_TRUE(app.start_menu().bots().size() == 2);
            click(app, MenuId::Continue);
            app.pump_network(0.01f);
            ASSERT_EQ(app.state(), AppState::MapSelect);
            click_fog(app, true);                                                            // the setup screen's fog option: a bot would see through it
            ASSERT_TRUE(app.map_select().is_fog_of_war_enabled());
            app.map_select().handle_key_down(SDLK_RETURN);
            ASSERT_EQ(app.state(), AppState::MapSelect);                                     // refused
            ASSERT_TRUE(app.bots() == nullptr);
            refusal = app.map_select().room().status;
            ASSERT_HAS(refusal, "Fog of War");
            click_fog(app, false);
            app.map_select().handle_key_down(SDLK_RETURN);
            ASSERT_EQ(app.state(), AppState::Playing);
            ASSERT_TRUE(app.bots() != nullptr);
            ASSERT_FALSE(app.sim().is_fog_of_war_enabled());
            ASSERT_EQ(app.sim().roster_mask(), 0x0B);                                        // the seats 0, 1 and 3; the empty seat has no hill
            ASSERT_EQ(app.sim().get_player_name(1), std::string("Bot (Medium)"));
            ASSERT_EQ(app.sim().get_player_name(3), std::string("Bot (Hard)"));
            via_menu = snapshot(app);
        }
        Played by_flags;
        {
            std::vector<std::string> args = {"ants", "--headless", "--bot", "1:medium", "--bot", "3:hard", "--settings", temp.file("b.ini")};
            std::vector<char*> storage;
            const ApplicationConfig cfg = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, storage));
            ASSERT_FALSE(cfg.start_menu);
            Application app;
            ASSERT_TRUE(app.init(cfg));
            click_fog(app, true);
            app.map_select().handle_key_down(SDLK_RETURN);
            ASSERT_EQ(app.map_select().room().status, refusal);                              // the same refusal, in the same words
            click_fog(app, false);
            app.map_select().handle_key_down(SDLK_RETURN);
            ASSERT_EQ(app.state(), AppState::Playing);
            by_flags = snapshot(app);
        }
        ASSERT_TRUE(via_menu.bots && via_menu.bot_seats == 0x0A);
        ASSERT_TRUE(same(via_menu, by_flags));
        ASSERT_TRUE(via_menu.decisions1 >= 4 && via_menu.decisions3 >= 20);                  // the bots ran
        // and only after the "Get ready" dialog: it was up for the ticks 1 .. 99 and gone on the 100th, and in those ticks neither bot looked or sent anything (the start hold), by the menu and by the flags
        ASSERT_TRUE(via_menu.dialog_up_during_99_ticks && by_flags.dialog_up_during_99_ticks);
        ASSERT_TRUE(via_menu.looks_in_the_dialog == 0 && via_menu.orders_in_the_dialog == 0 && by_flags.looks_in_the_dialog == 0 && by_flags.orders_in_the_dialog == 0);
        // the player's own seat can be another one (--player 2): the rows are the other three
        {
            Application app;
            ApplicationConfig cfg = menu_config(server.address(), temp.file("c.ini"));
            cfg.local_player_id = 2;
            ASSERT_TRUE(app.init(cfg));
            ASSERT_EQ(app.start_menu().own_seat(), 2);
            click(app, MenuId::Single);
            MenuElement e;
            ASSERT_FALSE(app.start_menu().find_element(MenuId::Seat2, e));
            click(app, MenuId::Seat0);
            ASSERT_EQ(app.start_menu().bots().size(), static_cast<size_t>(1));
            ASSERT_EQ(app.start_menu().bots()[0].seat, 0);
        }
    } TEST_END();

    TEST_CASE("A3.1 Join a demo room as the second player: Connecting, then the quick help (the option's default), then the guest's screen with the room as the server has it; the window's title carries the code; the third player fills the room and the server starts the match") {
        TempDir temp;
        Server server;
        const std::string code = "demo-tiny-3p-aaaaaa";
        Peer ann;
        ASSERT_TRUE(ann.join("127.0.0.1", server.port(), "Ann", code));                      // the first to join makes the demo room and leads it
        Application app;
        Hall hall{&server, &app, {&ann}, nullptr, false};
        ASSERT_TRUE(hall.until([&]() { return ann.net.phase() == net::NetGame::Phase::Room && ann.net.is_leader(); }, 8000));
        ASSERT_TRUE(app.init(menu_config(server.address(), temp.file("s.ini"))));
        ASSERT_EQ(app.window_title(), std::string("Ants"));
        menu_join(app, "Bob", code);
        ASSERT_EQ(app.start_menu().panel(), MenuPanel::Connecting);
        ASSERT_HAS(app.start_menu().elements()[1].text, "Connecting to " + server.address() + "...");
        ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::QuickHelp; }, 8000));
        ASSERT_TRUE(app.network_active());
        ASSERT_EQ(app.net()->phase(), net::NetGame::Phase::Room);
        ASSERT_EQ(app.net()->my_seat(), 1);
        ASSERT_FALSE(app.net()->is_leader());
        ASSERT_EQ(app.window_title(), "Ants - room " + code);
        ASSERT_EQ(app.hud().get_player_name(), std::string("Bob"));                          // the name typed for the room is the player's name on the HUD (labels, chat), as --name is
        app.quick_help_key(SDLK_RETURN);
        ASSERT_EQ(app.state(), AppState::MapSelect);
        hall.step(600);
        ASSERT_TRUE(app.map_select().is_guest());                                            // the guest's screen: only Leave works
        ASSERT_FALSE(app.map_select().has_start_button());
        const auto& room = app.map_select().room();
        ASSERT_TRUE(room.networked && room.my_seat == 1 && room.seats[0].occupied && room.seats[0].name == "Ann" && room.seats[1].name == "Bob");
        ASSERT_EQ(room.status, std::string(sim::strings::text(sim::strings::kWaitingForHost)));
        ASSERT_EQ(server.status(code).joined, 2);
        ASSERT_EQ(server.status(code).names[1], std::string("Bob"));
        // the third player fills the room: the server starts the match without anybody pressing START, and the application plays it as seat 1
        Peer cat;
        hall.peers.push_back(&cat);
        ASSERT_TRUE(cat.join("127.0.0.1", server.port(), "Cat", code));
        ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing && ann.net.phase() == net::NetGame::Phase::Playing && cat.net.phase() == net::NetGame::Phase::Playing; }, 20000));
        ASSERT_EQ(app.local_player_id(), 1);
        ASSERT_EQ(app.sim().roster_mask(), 0x07);
        ASSERT_EQ(app.sim().get_player_name(1), std::string("Bob"));
        ASSERT_EQ(app.window_title(), "Ants - room " + code);                               // the whole match
        hall.step(2000);
        ASSERT_TRUE(hall.identical(app.sim(), ann.sim));
    } TEST_END();

    TEST_CASE("A3.2 Join as the first player: the room's leader has the host's screen with START (v0.0.93); a second client joins and START starts the match for both") {
        TempDir temp;
        write_no_quick_help(temp.file("s.ini"));
        Server server;
        const std::string code = "demo-tiny-3p-bbbbbb";
        Application app;
        Peer bob;
        Hall hall{&server, &app, {&bob}, nullptr, false};
        ASSERT_TRUE(app.init(menu_config(server.address(), temp.file("s.ini"))));
        menu_join(app, "Leader", code);
        ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::MapSelect && app.net() != nullptr && app.net()->phase() == net::NetGame::Phase::Room && app.net()->is_leader(); }, 8000));
        hall.step(600);
        ASSERT_TRUE(app.map_select().leads_server_room() && app.map_select().has_start_button() && !app.map_select().is_guest());
        ASSERT_EQ(app.map_select().room().status, std::string(sim::strings::text(sim::strings::kPressStart)));
        ASSERT_EQ(server.status(code).leader, 0);
        ASSERT_TRUE(bob.join("127.0.0.1", server.port(), "Bob", code));
        ASSERT_TRUE(hall.until([&]() { return bob.net.phase() == net::NetGame::Phase::Room && app.net()->room().slots[1].state == net::SlotState::Client; }, 8000));
        hall.step(300);
        ASSERT_EQ(app.state(), AppState::MapSelect);                                         // two of the three seats: nobody starts it but the leader
        app.map_select().handle_key_down(SDLK_RETURN);                                      // START
        ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 20000));
        ASSERT_EQ(app.local_player_id(), 0);
        ASSERT_EQ(app.sim().roster_mask(), 0x03);
        hall.step(2000);
        ASSERT_TRUE(hall.identical(app.sim(), bob.sim));
    } TEST_END();

    TEST_CASE("A3.3 Join the last seat of a room: the server starts the match at once, Welcome, Room and Start arrive together, and the menu takes that for what it is (the player is in the room and the match is loading), never for a lost connection") {
        TempDir temp;
        write_no_quick_help(temp.file("s.ini"));
        Server server;
        const std::string code = "demo-tiny-2p-last01";
        Peer ann;
        Application app;
        Hall hall{&server, &app, {&ann}, nullptr, false};
        ASSERT_TRUE(ann.join("127.0.0.1", server.port(), "Ann", code));
        ASSERT_TRUE(hall.until([&]() { return ann.net.phase() == net::NetGame::Phase::Room; }, 8000));
        ASSERT_TRUE(app.init(menu_config(server.address(), temp.file("s.ini"))));
        menu_join(app, "Bob", code);
        ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing && ann.net.phase() == net::NetGame::Phase::Playing; }, 20000));
        ASSERT_TRUE(app.start_menu().message().empty());
        ASSERT_EQ(app.local_player_id(), 1);
        ASSERT_EQ(app.sim().roster_mask(), 0x03);
        ASSERT_EQ(app.window_title(), "Ants - room " + code);
        hall.step(2000);
        ASSERT_TRUE(hall.identical(app.sim(), ann.sim));
    } TEST_END();

    TEST_CASE("A4.1 Host an online match: the code is demo-<map>-<n>p-<six>, shown with Copy; the player is the first in the room and its leader; a second client joins with the code; Continue leads to the leader's screen and START starts a two-player match; both machines stay identical") {
        TempDir temp;
        write_no_quick_help(temp.file("s.ini"));
        Server server;
        std::vector<std::string> copied;
        Application app;
        Peer guest;
        Hall hall{&server, &app, {&guest}, nullptr, false};
        ApplicationConfig cfg = menu_config(server.address(), temp.file("s.ini"));
        cfg.clipboard_set = [&copied](const std::string& text) {
            copied.push_back(text);
            return true;
        };
        ASSERT_TRUE(app.init(cfg));
        menu_host(app, "Hostess", 1, 2);                                                     // Small, three players
        ASSERT_EQ(app.start_menu().panel(), MenuPanel::Connecting);
        ASSERT_TRUE(hall.until([&]() { return on_panel(app, MenuPanel::Room); }, 8000));
        const std::string code = shown_code(app);
        ASSERT_TRUE(code.rfind("demo-small-3p-", 0) == 0 && code.size() == 20);              // the page's grammar: demo-<map>-<n>p-<six>
        for (size_t i = 14; i < code.size(); ++i) ASSERT_TRUE(std::string(kRoomCodeAlphabet).find(code[i]) != std::string::npos);
        ASSERT_EQ(app.window_title(), "Ants - room " + code);                               // from the moment the player is in the room
        ASSERT_EQ(app.hud().get_player_name(), std::string("Hostess"));                      // and the name typed for the room is the player's name on the HUD
        const server::RoomStatus made = server.status(code);
        ASSERT_TRUE(made.map == "SMALL.LVL" && made.expected == 3 && made.joined == 1 && made.names[0] == "Hostess" && made.leader == 0);
        ASSERT_TRUE(app.net() != nullptr && app.net()->is_leader() && app.net()->my_seat() == 0);
        // the code is shown to share: Copy puts it on the clipboard
        MenuElement big;
        for (const MenuElement& e : app.start_menu().elements()) {
            if (e.kind == MenuKind::Code) big = e;
        }
        ASSERT_EQ(big.text, code);
        click(app, MenuId::Copy);
        ASSERT_TRUE(copied.size() == 1 && copied[0] == code);
        // a second client joins with the code: the count on the panel follows
        ASSERT_TRUE(guest.join("127.0.0.1", server.port(), "Guest", code));
        ASSERT_TRUE(hall.until([&]() { return guest.net.phase() == net::NetGame::Phase::Room; }, 8000));
        hall.step(100);
        bool counted = false;
        for (const MenuElement& e : app.start_menu().elements()) counted = counted || e.text == "Players in the room: 2 of 3";
        ASSERT_TRUE(counted);
        ASSERT_EQ(guest.net.room().slots[0].name, std::string("Hostess"));
        // Continue to the room: the leader's screen with START (no quick help: the option is off here)
        click(app, MenuId::EnterRoom);
        hall.step(20);
        ASSERT_EQ(app.state(), AppState::MapSelect);
        hall.step(600);
        ASSERT_TRUE(app.map_select().leads_server_room() && app.map_select().has_start_button());
        ASSERT_EQ(app.map_select().room().map_file, std::string("SMALL.LVL"));
        ASSERT_TRUE(app.map_select().room().seats[0].name == "Hostess" && app.map_select().room().seats[1].name == "Guest");
        app.map_select().handle_key_down(SDLK_RETURN);                                      // START: two of the three seats are taken
        ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing && guest.net.phase() == net::NetGame::Phase::Playing; }, 20000));
        ASSERT_EQ(app.sim().roster_mask(), 0x03);
        ASSERT_TRUE(server.status(code).state == server::RoomState::Running);
        ASSERT_EQ(app.window_title(), "Ants - room " + code);                               // the whole match
        hall.step(3000);
        ASSERT_TRUE(hall.identical(app.sim(), guest.sim));
    } TEST_END();

    TEST_CASE("A4.2 Host: Esc / Back on the room's panel leaves the room (the server sees the player go, the title is the program's again) and returns to the Host panel; the server that cannot make a room (its demo rooms are all taken) is told in words; a single-player game after that is the local player's, not the name that was typed for the room") {
        TempDir temp;
        write_no_quick_help(temp.file("s.ini"));
        Server server(2);
        Application app;
        Peer other;
        Hall hall{&server, &app, {&other}, nullptr, false};
        ASSERT_TRUE(app.init(menu_config(server.address(), temp.file("s.ini"))));
        const std::string local_name = app.hud().get_player_name();                          // what a single-player game calls the player (the system user)
        ASSERT_FALSE(local_name.empty());
        menu_host(app, "Hostess", 0, 0);
        ASSERT_TRUE(hall.until([&]() { return on_panel(app, MenuPanel::Room); }, 8000));
        const std::string code = shown_code(app);
        ASSERT_EQ(app.window_title(), "Ants - room " + code);
        press(app, SDLK_ESCAPE);
        ASSERT_TRUE(hall.until([&]() { return on_panel(app, MenuPanel::Host); }, 4000));
        ASSERT_TRUE(app.net() == nullptr && !app.network_active());
        ASSERT_EQ(app.window_title(), std::string("Ants"));
        ASSERT_TRUE(hall.until([&]() { return server.status(code).joined == 0; }, 4000));    // the server saw the player go
        ASSERT_TRUE(app.start_menu().message().empty());
        // the server has two demo rooms: the first (left empty, it stays until it expires) and now one that somebody else makes; the next host attempt is refused
        ASSERT_TRUE(other.join("127.0.0.1", server.port(), "Other", "demo-tiny-2p-taken1"));
        ASSERT_TRUE(hall.until([&]() { return other.net.phase() == net::NetGame::Phase::Room; }, 8000));
        click(app, MenuId::Host);
        ASSERT_TRUE(hall.until([&]() { return failed_on(app, MenuPanel::Host); }, 8000));
        ASSERT_HAS(app.start_menu().message(), "busy");
        ASSERT_HAS(app.start_menu().message(), "Try again");
        ASSERT_TRUE(app.net() == nullptr);
        ASSERT_EQ(app.window_title(), std::string("Ants"));
        // the room was left: a single-player game from the menu is the local player's (the name that the room had is not carried into it)
        hall.peers.clear();
        press(app, SDLK_ESCAPE);                                                              // the Host panel's Back
        ASSERT_TRUE(on_panel(app, MenuPanel::Main));
        click(app, MenuId::Single);
        click(app, MenuId::Continue);
        app.pump_network(0.01f);
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_FALSE(app.map_select().room().networked);
        ASSERT_EQ(app.hud().get_player_name(), local_name);
        ASSERT_TRUE(app.hud().get_player_name() != "Hostess");
    } TEST_END();

    TEST_CASE("A4.3 Host: a room that fills up while its code is on the screen starts by itself (nobody has to press anything): the match begins, the title keeps the code, and Leave afterwards brings the player back to the menu") {
        TempDir temp;
        write_no_quick_help(temp.file("s.ini"));
        Server server;
        Application app;
        Peer guest;
        Hall hall{&server, &app, {&guest}, nullptr, false};
        ASSERT_TRUE(app.init(menu_config(server.address(), temp.file("s.ini"))));
        menu_host(app, "Hostess", 0, 1);                                                     // Tiny, two players
        ASSERT_TRUE(hall.until([&]() { return on_panel(app, MenuPanel::Room); }, 8000));
        const std::string code = shown_code(app);
        ASSERT_TRUE(guest.join("127.0.0.1", server.port(), "Guest", code));
        ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing && guest.net.phase() == net::NetGame::Phase::Playing; }, 20000));
        ASSERT_EQ(app.sim().roster_mask(), 0x03);
        ASSERT_EQ(app.local_player_id(), 0);
        ASSERT_EQ(app.window_title(), "Ants - room " + code);
        hall.step(2000);
        ASSERT_TRUE(hall.identical(app.sim(), guest.sim));
        // the player leaves the match through the quit dialog (one other side is left: that ends the match for both), and then Leave on the results
        hall.step(5000);
        SDL_KeyboardEvent q_ev{};
        q_ev.type = SDL_KEYDOWN;
        q_ev.keysym.sym = SDLK_q;
        q_ev.keysym.mod = KMOD_LCTRL;
        app.handle_key_down(q_ev);
        SDL_KeyboardEvent y_ev{};
        y_ev.type = SDL_KEYDOWN;
        y_ev.keysym.sym = SDLK_y;
        app.handle_key_down(y_ev);
        ASSERT_TRUE(hall.until([&]() { return app.scorecard().is_open() && app.sim().is_match_over(); }, 8000));
        app.update_results(0.3f);
        app.scorecard().leave();
        ASSERT_TRUE(app.is_running());
        ASSERT_EQ(app.state(), AppState::StartMenu);
        ASSERT_EQ(app.window_title(), std::string("Ants"));
        ASSERT_TRUE(app.net() == nullptr);
    } TEST_END();

    TEST_CASE("A4.4 Host: when the server holds a room with the very code that the menu made (one in 900 million), the player would be a guest of somebody else's room, not its leader: the menu says so, leaves the room again and nothing stays") {
        TempDir temp;
        Server server;
        const std::string taken = "demo-tiny-4p-aaaaaa";                                      // the code that a random source of all zeros makes: 'a' is the first character of the alphabet
        ASSERT_TRUE(server.make_room(taken, 4));
        Peer owner;
        Application app;
        Hall hall{&server, &app, {&owner}, nullptr, false};
        ApplicationConfig cfg = menu_config(server.address(), temp.file("s.ini"));
        cfg.room_code_random = []() { return 0u; };
        ASSERT_TRUE(app.init(cfg));
        ASSERT_TRUE(owner.join("127.0.0.1", server.port(), "Owner", taken));
        ASSERT_TRUE(hall.until([&]() { return owner.net.phase() == net::NetGame::Phase::Room && owner.net.is_leader(); }, 8000));
        menu_host(app, "Hostess", 0, 0);                                                     // Tiny, four players: the code is the taken one
        ASSERT_TRUE(hall.until([&]() { return failed_on(app, MenuPanel::Host); }, 8000));
        ASSERT_HAS(app.start_menu().message(), "room code was taken");
        ASSERT_TRUE(app.net() == nullptr);
        ASSERT_EQ(app.window_title(), std::string("Ants"));
        ASSERT_TRUE(hall.until([&]() { return server.status(taken).joined == 1; }, 4000));    // the owner is alone in its room again: the menu's player left it
        ASSERT_TRUE(owner.net.is_leader());
    } TEST_END();

    TEST_CASE("A4.5 Host: the server closes the room while its code is on the screen: back to the Host panel with a line of its own (The server closed the room), nothing of the room stays, and the same panel can host again") {
        TempDir temp;
        Server server;
        Application app;
        Hall hall{&server, &app, {}, nullptr, false};
        ASSERT_TRUE(app.init(menu_config(server.address(), temp.file("s.ini"))));
        menu_host(app, "Hostess", 0, 1);
        ASSERT_TRUE(hall.until([&]() { return on_panel(app, MenuPanel::Room); }, 8000));
        const std::string code = shown_code(app);
        ASSERT_TRUE(server.mgr.close_room(code, server.now));
        ASSERT_TRUE(hall.until([&]() { return failed_on(app, MenuPanel::Host); }, 8000));
        ASSERT_EQ(app.start_menu().message(), std::string("The server closed the room."));
        ASSERT_TRUE(app.net() == nullptr && !app.network_active());
        ASSERT_EQ(app.window_title(), std::string("Ants"));
        click(app, MenuId::Host);                                                            // the same panel hosts again (a new code)
        ASSERT_TRUE(hall.until([&]() { return on_panel(app, MenuPanel::Room); }, 8000));
        ASSERT_TRUE(shown_code(app) != code);
    } TEST_END();

    TEST_CASE("A4.6 Host: with no map stored the panel opens on Treasure and a room that is made without choosing a map is a Treasure room (the code names it, the server makes it on TREASURE.LVL for four players); the default is not written to the settings; a map that is stored wins after a restart (the room is made on it)") {
        TempDir temp;
        const std::string settings = ini(temp, "t.ini");
        {   // nothing stored (the fixture server's own default map is TINY.LVL: a Treasure room can only come from the code that the menu made)
            Server server;
            Application app;
            Hall hall{&server, &app, {}, nullptr, false};
            ASSERT_TRUE(app.init(menu_config(server.address(), settings)));
            click(app, MenuId::HostOnline);
            MenuElement e;
            ASSERT_TRUE(app.start_menu().find_element(MenuId::HostMap, e) && e.value == "Treasure");
            ASSERT_EQ(app.start_menu().settings().host_map, 4);
            fill(app, MenuId::HostName, "Hostess");
            click(app, MenuId::Host);
            ASSERT_TRUE(hall.until([&]() { return on_panel(app, MenuPanel::Room); }, 8000));
            const std::string code = shown_code(app);
            ASSERT_TRUE(code.rfind("demo-treasure-4p-", 0) == 0 && code.size() == 23);       // the page's grammar: demo-<map>-<n>p-<six>
            const server::RoomStatus made = server.status(code);
            ASSERT_TRUE(made.map == "TREASURE.LVL" && made.expected == 4 && made.joined == 1 && made.leader == 0);
        }
        ASSERT_TRUE(read_file(settings).find("host_map") == std::string::npos);              // nothing was chosen, so nothing was written: the default is the program's, not the player's
        {   // a stored choice wins
            {
                std::ofstream out(settings, std::ios::app | std::ios::binary);
                out << "host_map=gauntlet\n";
            }
            Server server;
            Application app;
            Hall hall{&server, &app, {}, nullptr, false};
            ASSERT_TRUE(app.init(menu_config(server.address(), settings)));
            click(app, MenuId::HostOnline);
            MenuElement e;
            ASSERT_TRUE(app.start_menu().find_element(MenuId::HostMap, e) && e.value == "Gauntlet");
            ASSERT_EQ(app.start_menu().settings().host_map, 3);
            fill(app, MenuId::HostName, "Hostess");
            click(app, MenuId::Host);
            ASSERT_TRUE(hall.until([&]() { return on_panel(app, MenuPanel::Room); }, 8000));
            const std::string code = shown_code(app);
            ASSERT_TRUE(code.rfind("demo-gauntlet-4p-", 0) == 0);
            ASSERT_EQ(server.status(code).map, std::string("GAUNTLET.LVL"));
        }
    } TEST_END();

    TEST_CASE("A5.1 Failures come back to the panel with a clear line, every one different: the server cannot be reached (named), no room with that code (named), a name the server would rename is refused on the panel without any connection, and the lost connection") {
        TempDir temp;
        Server server;
        Application app;
        RawServer raw;
        Hall hall{&server, &app, {}, &raw, false};
        ASSERT_TRUE(app.init(menu_config(server.address(), temp.file("s.ini"))));
        // a port that nothing listens on
        const uint16_t closed = []() {
            auto l = net::TcpListener::listen(0, true);
            return l ? l->port() : uint16_t{1};
        }();
        app.start_menu().set_server(ServerAddress{"127.0.0.1", closed});
        menu_join(app, "Dave", "abc-123");
        ASSERT_TRUE(hall.until([&]() { return failed_on(app, MenuPanel::Join); }, 8000));
        ASSERT_HAS(app.start_menu().message(), "Cannot reach 127.0.0.1:" + std::to_string(closed));
        ASSERT_TRUE(app.net() == nullptr && !app.network_active());                          // nothing of the attempt stays
        ASSERT_EQ(app.start_menu().name(), std::string("Dave"));                             // the fields are as they were
        ASSERT_EQ(app.start_menu().code(), std::string("abc-123"));
        // the real server: no room with that code (the case of the code is kept: the server tells the code that was sent)
        app.start_menu().set_server(ServerAddress{"127.0.0.1", server.port()});
        fill(app, MenuId::Code, "Zz-Nope-1");
        click(app, MenuId::Join);
        ASSERT_TRUE(hall.until([&]() { return failed_on(app, MenuPanel::Join); }, 8000));
        ASSERT_HAS(app.start_menu().message(), "no room with the code Zz-Nope-1");
        ASSERT_HAS(app.start_menu().message(), server.address());
        ASSERT_HAS(app.start_menu().message(), "capital letters matter");
        // a name that looks like a bot's never reaches any server (the one that would rename it is not asked)
        app.start_menu().set_server(ServerAddress{"127.0.0.1", raw.port()});
        fill(app, MenuId::Name, "Bot (Easy)");
        click(app, MenuId::Join);
        hall.step(300);
        ASSERT_EQ(app.start_menu().panel(), MenuPanel::Join);
        ASSERT_HAS(app.start_menu().message(), "Bot (");
        ASSERT_EQ(raw.accepted, 0);
        // the server hangs up on the Hello: the connection was lost before the room
        raw.mode = RawServer::Mode::HangUp;
        fill(app, MenuId::Name, "Dave");
        click(app, MenuId::Join);
        ASSERT_TRUE(hall.until([&]() { return failed_on(app, MenuPanel::Join); }, 8000));
        ASSERT_HAS(app.start_menu().message(), "lost before you were in the room");
        ASSERT_HAS(app.start_menu().message(), "127.0.0.1:" + std::to_string(raw.port()));
        ASSERT_EQ(raw.hellos.size(), static_cast<size_t>(1));
        ASSERT_TRUE(raw.hellos[0].name == "Dave" && raw.hellos[0].room == "Zz-Nope-1" && raw.hellos[0].version == net::kProtocolVersion);   // what the server heard: the name and the code as typed
        ASSERT_TRUE(app.net() == nullptr);
    } TEST_END();

    TEST_CASE("A5.2 Every reason that a server can answer with is told in its own words: room full, another version (this game's version named), the match already running, removed, bad request, no such room, and (protocol 10) dropped, rejoin failed, taken over; hosting that is refused by NoSuchRoom says the server is busy") {
        TempDir temp;
        Application app;
        RawServer raw;
        Hall hall{nullptr, &app, {}, &raw, false};
        ASSERT_TRUE(app.init(menu_config("127.0.0.1:" + std::to_string(raw.port()), temp.file("s.ini"))));
        struct Case {
            net::RejectReason reason;
            const char* has;
        };
        const Case cases[] = {
            {net::RejectReason::Full, "is full"},
            {net::RejectReason::VersionMismatch, "another version"},
            {net::RejectReason::MatchRunning, "already started"},
            {net::RejectReason::Kicked, "removed"},
            {net::RejectReason::BadRequest, "did not accept"},
            {net::RejectReason::NoSuchRoom, "no room with the code"},
            {net::RejectReason::Dropped, "dropped from the game"},                       // (protocol 10: the answers to a Hello with a key; the menu's own never shows one, but a server may send them)
            {net::RejectReason::RejoinFailed, "could not be rejoined"},
            {net::RejectReason::Superseded, "taken over by another window"},
        };
        raw.mode = RawServer::Mode::Reject;
        std::vector<std::string> seen;
        for (const Case& c : cases) {
            raw.reason = c.reason;
            if (app.start_menu().panel() != MenuPanel::Main) press(app, SDLK_ESCAPE);
            const std::string code = "room-" + std::to_string(static_cast<int>(c.reason));
            menu_join(app, "Dave", code);
            ASSERT_TRUE(hall.until([&]() { return failed_on(app, MenuPanel::Join); }, 8000));
            const std::string message = app.start_menu().message();
            ASSERT_HAS(message, c.has);
            if (c.reason == net::RejectReason::VersionMismatch) ASSERT_HAS(message, std::string(VERSION_STRING));
            if (c.reason == net::RejectReason::Full || c.reason == net::RejectReason::NoSuchRoom) ASSERT_HAS(message, code);
            ASSERT_TRUE(std::find(seen.begin(), seen.end(), message) == seen.end());          // each reason has its own line
            seen.push_back(message);
            ASSERT_TRUE(app.net() == nullptr);
        }
        press(app, SDLK_ESCAPE);
        // hosting: the demo room that the server cannot make is NoSuchRoom, which is told as "busy" (there is no room to look for: the code is the player's own)
        raw.reason = net::RejectReason::NoSuchRoom;
        menu_host(app, "Hostess", 0, 0);
        ASSERT_TRUE(hall.until([&]() { return failed_on(app, MenuPanel::Host); }, 8000));
        ASSERT_HAS(app.start_menu().message(), "busy");
        ASSERT_TRUE(app.net() == nullptr);
        // and a version mismatch of a host attempt is the same words as a join's
        raw.reason = net::RejectReason::VersionMismatch;
        click(app, MenuId::Host);
        ASSERT_TRUE(hall.until([&]() { return failed_on(app, MenuPanel::Host) && app.start_menu().message().find("another version") != std::string::npos; }, 8000));
    } TEST_END();

    TEST_CASE("A5.3 Cancel: Esc while the name is looked up and the Cancel button while the server has not answered end the attempt at once (no error, the panel as it was, nothing of the connection stays), and the next attempt works; a lookup that never ends, one that fails and a server that never answers end in a message") {
        TempDir temp;
        Server server;
        Application app;
        RawServer raw;
        Hall hall{&server, &app, {}, &raw, false};
        const auto lookup = std::make_shared<Lookup>();
        ApplicationConfig cfg = menu_config("slow.example.org:" + std::to_string(raw.port()), temp.file("s.ini"));
        cfg.host_resolver = lookup_of(lookup);
        cfg.menu_connect_timeout_ms = 3000;
        ASSERT_TRUE(app.init(cfg));
        // (1) cancelled during the name lookup: Esc
        menu_join(app, "Dave", "abc-1");
        hall.step(100);
        ASSERT_EQ(app.start_menu().panel(), MenuPanel::Connecting);
        ASSERT_HAS(app.start_menu().elements()[1].text, "Connecting to slow.example.org:" + std::to_string(raw.port()) + "...");
        ASSERT_TRUE(app.net() == nullptr);
        press(app, SDLK_ESCAPE);
        hall.step(20);
        ASSERT_EQ(app.start_menu().panel(), MenuPanel::Join);
        ASSERT_TRUE(app.start_menu().message().empty());                                     // a cancel is no error
        ASSERT_TRUE(app.net() == nullptr && !app.network_active());
        lookup->release = true;                                                              // the abandoned lookup answers now: nothing happens
        hall.step(300);
        ASSERT_EQ(app.start_menu().panel(), MenuPanel::Join);
        ASSERT_TRUE(app.net() == nullptr);
        ASSERT_EQ(raw.accepted, 0);
        // (2) cancelled while the server has not answered (it accepted and says nothing): the Cancel button
        click(app, MenuId::Join);
        ASSERT_TRUE(hall.until([&]() { return raw.accepted == 1 && app.net() != nullptr; }, 8000));
        hall.step(200);
        ASSERT_EQ(app.start_menu().panel(), MenuPanel::Connecting);
        ASSERT_EQ(raw.connected(), 1);
        click(app, MenuId::Cancel);
        hall.step(100);
        ASSERT_EQ(app.start_menu().panel(), MenuPanel::Join);
        ASSERT_TRUE(app.start_menu().message().empty());
        ASSERT_TRUE(app.net() == nullptr && !app.network_active());
        ASSERT_TRUE(hall.until([&]() { return raw.connected() == 0; }, 4000));               // the connection is closed
        // (3) the next attempt works: the real server answers (no such room)
        app.start_menu().set_server(ServerAddress{"127.0.0.1", server.port()});
        click(app, MenuId::Join);
        ASSERT_TRUE(hall.until([&]() { return failed_on(app, MenuPanel::Join); }, 8000));
        ASSERT_HAS(app.start_menu().message(), "no room with the code abc-1");
        // (4) a lookup that never ends: the time limit of the attempt says so
        lookup->release = false;
        app.start_menu().set_server(ServerAddress{"slow.example.org", raw.port()});
        click(app, MenuId::Join);
        ASSERT_TRUE(hall.until([&]() { return failed_on(app, MenuPanel::Join) && app.start_menu().message().find("did not answer") != std::string::npos; }, 8000));
        ASSERT_HAS(app.start_menu().message(), "The server slow.example.org:" + std::to_string(raw.port()) + " did not answer");
        ASSERT_TRUE(app.net() == nullptr);
        lookup->release = true;
        hall.step(100);
        ASSERT_EQ(app.start_menu().panel(), MenuPanel::Join);
        // (5) a name that cannot be found
        lookup->mode = 1;
        click(app, MenuId::Join);
        ASSERT_TRUE(hall.until([&]() { return failed_on(app, MenuPanel::Join) && app.start_menu().message().find("Cannot find") != std::string::npos; }, 8000));
        ASSERT_HAS(app.start_menu().message(), "Cannot find slow.example.org");
        ASSERT_TRUE(lookup->calls >= 3);
        // (6) a server that accepts and never answers: the time limit of the attempt ends the wait
        app.start_menu().set_server(ServerAddress{"127.0.0.1", raw.port()});
        click(app, MenuId::Join);
        ASSERT_TRUE(hall.until([&]() { return failed_on(app, MenuPanel::Join); }, 30000));
        ASSERT_HAS(app.start_menu().message(), "did not answer");
        ASSERT_TRUE(app.net() == nullptr);
    } TEST_END();

    TEST_CASE("A5.4 What a real server refuses: a room whose match has started is 'already started'; on a server without demo rooms a demo-... code is no room (the code is named); the first player of a room that exists joins at once") {
        TempDir temp;
        Server server(0);                                                                    // no demo rooms: only the rooms that somebody made
        ASSERT_TRUE(server.make_room("run-room-1", 2));
        Peer ann;
        Peer bob;
        Application app;
        Hall hall{&server, &app, {&ann, &bob}, nullptr, false};
        ASSERT_TRUE(app.init(menu_config(server.address(), temp.file("s.ini"))));
        ASSERT_TRUE(ann.join("127.0.0.1", server.port(), "Ann", "run-room-1"));
        ASSERT_TRUE(hall.until([&]() { return ann.net.phase() == net::NetGame::Phase::Room; }, 8000));
        ASSERT_TRUE(bob.join("127.0.0.1", server.port(), "Bob", "run-room-1"));
        ASSERT_TRUE(hall.until([&]() { return ann.net.phase() == net::NetGame::Phase::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 20000));
        menu_join(app, "Dave", "run-room-1");
        ASSERT_TRUE(hall.until([&]() { return failed_on(app, MenuPanel::Join); }, 8000));
        ASSERT_HAS(app.start_menu().message(), "already started");
        ASSERT_TRUE(app.net() == nullptr);
        // a demo code on a server that makes no demo rooms
        fill(app, MenuId::Code, "demo-tiny-2p-nothing");
        click(app, MenuId::Join);
        ASSERT_TRUE(hall.until([&]() { return failed_on(app, MenuPanel::Join) && app.start_menu().message().find("demo-tiny-2p-nothing") != std::string::npos; }, 8000));
        ASSERT_HAS(app.start_menu().message(), "no room with the code demo-tiny-2p-nothing");
        // hosting there is refused in the words of a busy server
        press(app, SDLK_ESCAPE);
        menu_host(app, "Hostess", 0, 0);
        ASSERT_TRUE(hall.until([&]() { return failed_on(app, MenuPanel::Host); }, 8000));
        ASSERT_HAS(app.start_menu().message(), "busy");
    } TEST_END();

    TEST_CASE("A6.1 After a network match: the results screen, then Leave: back at the start menu (not the end of the program), nothing of the game stays (the room, the net, the title, the names), and the menu works again: a single-player game can be started") {
        TempDir temp;
        write_no_quick_help(temp.file("s.ini"));
        Server server;
        Application app;
        Peer guest;
        Hall hall{&server, &app, {&guest}, nullptr, false};
        ASSERT_TRUE(app.init(menu_config(server.address(), temp.file("s.ini"))));
        const std::string local_name = app.sim().get_player_name(0);                         // what a single-player game calls the player (the system user)
        ASSERT_FALSE(local_name.empty());
        std::string report;
        ASSERT_TRUE(host_join_start(hall, app, guest, "127.0.0.1", server.port(), report));
        const std::string code = server.mgr.list(server.now)[0].code;
        ASSERT_EQ(app.window_title(), "Ants - room " + code);
        hall.step(6000);                                                                     // the "get ready" dialog is over
        ASSERT_FALSE(app.sim().is_match_over());
        sim::Command quit;                                                                   // the guest quits the two-player match: the match is over
        quit.type = sim::CommandType::Quit;
        quit.issuer = 1;
        guest.net.submit(quit);
        ASSERT_TRUE(hall.until([&]() { return app.sim().is_match_over() && app.scorecard().is_open(); }, 8000));
        app.update_results(0.3f);
        ASSERT_FALSE(app.scorecard().rows().empty());
        ASSERT_EQ(app.state(), AppState::Playing);                                           // the results are on top of the match; nothing has happened to the program
        ASSERT_TRUE(app.is_running());
        ASSERT_TRUE(app.audio_mixer().music_filepath().find("INTRO") == std::string::npos);  // a match plays one of the game's pieces
        StartMenu layout;                                                                    // where the first panel's entries lie
        layout.show_main();
        MenuElement join_entry;
        ASSERT_TRUE(layout.find_element(MenuId::JoinWithCode, join_entry));
        app.handle_menu_event(mouse_event(SDL_MOUSEMOTION, join_entry.rect.x + 20, join_entry.rect.y + 20));   // the pointer rests where "Join with a code" will be (the last selection of the first panel was "Host an online match")
        app.scorecard().leave();                                                             // Leave (the button, Enter, C, Q, X)
        ASSERT_TRUE(app.is_running());
        ASSERT_EQ(app.state(), AppState::StartMenu);
        ASSERT_EQ(app.start_menu().panel(), MenuPanel::Main);
        ASSERT_EQ(app.start_menu().selected(), MenuId::JoinWithCode);                        // the menu selects what the resting pointer is on, as every screen does
        ASSERT_TRUE(app.audio_mixer().music_filepath().find("INTRO") != std::string::npos);  // the menu has the intro music, not the match's piece
        ASSERT_TRUE(app.start_menu().message().empty());
        ASSERT_TRUE(app.net() == nullptr && !app.network_active());
        ASSERT_FALSE(app.scorecard().is_open());
        ASSERT_EQ(app.window_title(), std::string("Ants"));
        ASSERT_TRUE(app.bots() == nullptr);
        ASSERT_EQ(app.hud().get_player_name(), local_name);                                  // the name that was typed for the room is gone from the screens at once
        // the menu works again: a single-player game from here (the local name is back)
        hall.peers.clear();
        click(app, MenuId::Single);
        click(app, MenuId::Continue);
        app.pump_network(0.01f);
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_FALSE(app.map_select().room().networked);
        ASSERT_EQ(app.local_player_id(), 0);
        app.map_select().handle_key_down(SDLK_RETURN);
        ASSERT_EQ(app.state(), AppState::Playing);
        ASSERT_EQ(app.sim().roster_mask(), 0x0F);                                            // the original's single-player game again
        ASSERT_FALSE(app.network_active());
        ASSERT_TRUE(app.sim().get_player_name(0) != "Hostess" && !app.sim().get_player_name(0).empty());   // the local name is back: the name that was typed for the room is not the single player's
        ASSERT_EQ(app.sim().get_player_name(0), local_name);
    } TEST_END();

    TEST_CASE("A6.2 A network game that is lost or left goes back to the menu too: the server closes the room in the middle of the match (the notice names it), a room that dies under the player, Leave on the room's screen, and the quit dialog's Yes; without the menu the same Leave ends the program as it always did") {
        TempDir temp;
        write_no_quick_help(temp.file("s.ini"));
        Server server(16);
        {   // the room is closed under the players: the match cannot go on
            Application app;
            Peer guest;
            Hall hall{&server, &app, {&guest}, nullptr, false};
            ASSERT_TRUE(app.init(menu_config(server.address(), temp.file("s.ini"))));
            std::string report;
            ASSERT_TRUE(host_join_start(hall, app, guest, "127.0.0.1", server.port(), report));
            const std::string code = server.mgr.list(server.now)[0].code;
            hall.step(1000);
            ASSERT_TRUE(server.mgr.close_room(code, server.now));
            ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::StartMenu; }, 12000));
            ASSERT_TRUE(app.is_running());
            ASSERT_EQ(app.start_menu().panel(), MenuPanel::Main);
            ASSERT_HAS(app.start_menu().message(), "lost");                                  // the notice is on the first panel
            ASSERT_TRUE(app.net() == nullptr);
            ASSERT_EQ(app.window_title(), std::string("Ants"));
        }
        {   // Leave on the room's screen
            Application app;
            Hall hall{&server, &app, {}, nullptr, false};
            ASSERT_TRUE(app.init(menu_config(server.address(), temp.file("s.ini"))));
            menu_join(app, "Dave", "demo-tiny-4p-leave1");
            ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::MapSelect && app.net() != nullptr && app.net()->phase() == net::NetGame::Phase::Room; }, 8000));
            hall.step(600);
            app.map_select().handle_key_down(SDLK_q);                                         // Q / X / the Leave button: leave
            ASSERT_TRUE(app.is_running());
            ASSERT_EQ(app.state(), AppState::StartMenu);
            ASSERT_TRUE(app.net() == nullptr);
            ASSERT_EQ(app.window_title(), std::string("Ants"));
            ASSERT_TRUE(hall.until([&]() { return server.status("demo-tiny-4p-leave1").joined == 0; }, 4000));
        }
        {   // the room dies under a player who is on the room's screen (the server closed it): not a dead screen, the menu with the reason
            Application app;
            Hall hall{&server, &app, {}, nullptr, false};
            ASSERT_TRUE(app.init(menu_config(server.address(), temp.file("s.ini"))));
            menu_join(app, "Dave", "demo-tiny-4p-dead01");
            ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::MapSelect && app.net() != nullptr && app.net()->phase() == net::NetGame::Phase::Room; }, 8000));
            hall.step(600);
            ASSERT_TRUE(server.mgr.close_room("demo-tiny-4p-dead01", server.now));
            ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::StartMenu; }, 8000));
            ASSERT_FALSE(app.start_menu().message().empty());
            ASSERT_TRUE(app.net() == nullptr && app.is_running());
        }
        {   // the quit dialog's Yes of a match with more than one other side left (here a three-player room): back at the menu
            Application app;
            Peer ann;
            Peer bob;
            Hall hall{&server, &app, {&ann, &bob}, nullptr, false};
            ASSERT_TRUE(app.init(menu_config(server.address(), temp.file("s.ini"))));
            ASSERT_TRUE(ann.join("127.0.0.1", server.port(), "Ann", "demo-tiny-3p-dialog"));
            ASSERT_TRUE(hall.until([&]() { return ann.net.phase() == net::NetGame::Phase::Room; }, 8000));
            ASSERT_TRUE(bob.join("127.0.0.1", server.port(), "Bob", "demo-tiny-3p-dialog"));
            ASSERT_TRUE(hall.until([&]() { return bob.net.phase() == net::NetGame::Phase::Room; }, 8000));
            menu_join(app, "Dave", "demo-tiny-3p-dialog");
            const bool started = hall.until([&]() { return app.state() == AppState::Playing; }, 20000);
            if (!started) {
                const server::RoomStatus rs = server.status("demo-tiny-3p-dialog");
                std::cout << "\n    state " << static_cast<int>(app.state()) << " panel " << static_cast<int>(app.start_menu().panel()) << " message '" << app.start_menu().message()
                          << "' net phase " << (app.net() ? static_cast<int>(app.net()->phase()) : -1) << " room state " << static_cast<int>(rs.state) << " joined " << static_cast<int>(rs.joined) << " expected "
                          << static_cast<int>(rs.expected) << " ann " << static_cast<int>(ann.net.phase()) << " bob " << static_cast<int>(bob.net.phase()) << "\n";
            }
            ASSERT_TRUE(started);
            hall.step(6000);
            SDL_KeyboardEvent q_ev{};
            q_ev.type = SDL_KEYDOWN;
            q_ev.keysym.sym = SDLK_q;
            q_ev.keysym.mod = KMOD_LCTRL;
            app.handle_key_down(q_ev);
            ASSERT_TRUE(app.hud().is_quit_dialog_open());
            SDL_KeyboardEvent y_ev{};
            y_ev.type = SDL_KEYDOWN;
            y_ev.keysym.sym = SDLK_y;
            app.handle_key_down(y_ev);
            ASSERT_TRUE(app.is_running());
            ASSERT_EQ(app.state(), AppState::StartMenu);
            ASSERT_TRUE(app.net() == nullptr);
            ASSERT_EQ(app.window_title(), std::string("Ants"));
        }
        {   // a game without the menu: Leave is the end of the program, as it always was
            Application app;
            Hall hall{&server, &app, {}, nullptr, false};
            ApplicationConfig cfg = menu_config(server.address(), temp.file("s.ini"));
            cfg.start_menu = false;
            cfg.net_role = ApplicationConfig::NetRole::Join;
            cfg.net_address = "127.0.0.1";
            cfg.net_port = server.port();
            cfg.net_room = "demo-tiny-4p-nomenu";
            cfg.player_name = "Dave";
            ASSERT_TRUE(app.init(cfg));
            ASSERT_FALSE(app.start_menu_enabled());
            ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::MapSelect && app.net() != nullptr && app.net()->phase() == net::NetGame::Phase::Room; }, 8000));
            hall.step(600);
            app.map_select().handle_key_down(SDLK_q);
            ASSERT_FALSE(app.is_running());
            ASSERT_FALSE(app.network_active());
        }
    } TEST_END();

    TEST_CASE("A7.1 The window's title: 'Ants' at the menu, 'Ants - room <code>' from the moment the player is in a room until the player is back at the menu (a --title is kept in it), and the program's again after a failed attempt") {
        TempDir temp;
        write_no_quick_help(temp.file("s.ini"));
        Server server;
        Application app;
        Hall hall{&server, &app, {}, nullptr, false};
        ApplicationConfig cfg = menu_config(server.address(), temp.file("s.ini"));
        cfg.title = "Ants test";
        ASSERT_TRUE(app.init(cfg));
        ASSERT_EQ(app.window_title(), std::string("Ants test"));
        menu_join(app, "Dave", "demo-tiny-2p-title1");
        ASSERT_EQ(app.window_title(), std::string("Ants test"));                             // not yet: only the room's player has its code in the title
        ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::MapSelect; }, 8000));
        ASSERT_EQ(app.window_title(), std::string("Ants test - room demo-tiny-2p-title1"));
        app.map_select().handle_key_down(SDLK_x);
        ASSERT_EQ(app.state(), AppState::StartMenu);
        ASSERT_EQ(app.window_title(), std::string("Ants test"));
        // a failed attempt leaves it alone
        app.start_menu().set_server(ServerAddress{"127.0.0.1", 1});
        click(app, MenuId::JoinWithCode);
        click(app, MenuId::Join);
        ASSERT_TRUE(hall.until([&]() { return failed_on(app, MenuPanel::Join); }, 8000));
        ASSERT_EQ(app.window_title(), std::string("Ants test"));
    } TEST_END();

    TEST_CASE("A8.1 The panel refuses exactly the names that the server would rename: a battery of names is sent to a real server, and the ones the menu accepts come back as they were, the ones it refuses come back as 'Player N'") {
        Server server;
        const std::vector<std::string> names = {"Dave",        "Bobby",   "Robot",    "Bot",      "Bot Smith", "bot-9",   "Abot (x)", "Bot (Hard)", "bot (x)",  "BOT (Easy)",
                                                " Bot (x)",    "B o t (x)", "bot(x)", "  bOt(  ", "x",         "Mary Ann", "Zed and Co", "bot (",     "B O T ("};
        Hall hall{&server, nullptr, {}, nullptr, false};
        int n = 0;
        for (const std::string& name : names) {
            const std::string code = "name-battery-" + std::to_string(++n);
            ASSERT_TRUE(server.make_room(code, 2));
            Peer peer;
            hall.peers = {&peer};
            ASSERT_TRUE(peer.join("127.0.0.1", server.port(), name, code));
            ASSERT_TRUE(hall.until([&]() { return peer.net.phase() == net::NetGame::Phase::Room && server.status(code).joined == 1; }, 8000));
            const std::string at_server = server.status(code).names[0];
            std::string clean;
            std::string why;
            const bool accepted = check_player_name(name, clean, why);
            if (accepted) {
                if (at_server != clean) std::cout << "\n    '" << name << "' was accepted by the menu as '" << clean << "', the server shows '" << at_server << "'";
                ASSERT_EQ(at_server, clean);
            } else {
                if (at_server != "Player 1") std::cout << "\n    '" << name << "' was refused by the menu, the server shows '" << at_server << "'";
                ASSERT_EQ(at_server, std::string("Player 1"));
            }
            peer.net.leave();
            hall.peers.clear();
        }
    } TEST_END();

    TEST_CASE("A9.1 The clipboard: Cmd+V / Ctrl+V pastes what the system clipboard holds into the code (through the key events of the window), Copy hands the room's code to the system clipboard; a clipboard that fails says so") {
        TempDir temp;
        Server server;
        std::string clipboard = "  demo-tiny-2p-pasted \n";
        std::vector<std::string> written;
        bool writable = true;
        Application app;
        Hall hall{&server, &app, {}, nullptr, false};
        ApplicationConfig cfg = menu_config(server.address(), temp.file("s.ini"));
        cfg.clipboard_get = [&clipboard]() { return clipboard; };
        cfg.clipboard_set = [&](const std::string& text) {
            written.push_back(text);
            return writable;
        };
        ASSERT_TRUE(app.init(cfg));
        click(app, MenuId::JoinWithCode);
        click(app, MenuId::Code);
        press(app, SDLK_v, KMOD_GUI);
        ASSERT_EQ(app.start_menu().code(), std::string("demo-tiny-2p-pasted"));
        press(app, SDLK_a, KMOD_CTRL);
        clipboard = "another-1";
        press(app, SDLK_v, KMOD_CTRL);
        ASSERT_EQ(app.start_menu().code(), std::string("another-1"));
        press(app, SDLK_ESCAPE);
        menu_host(app, "Hostess", 0, 1);
        ASSERT_TRUE(hall.until([&]() { return on_panel(app, MenuPanel::Room); }, 8000));
        writable = false;
        click(app, MenuId::Copy);
        ASSERT_TRUE(written.size() == 1 && written[0] == shown_code(app));
        ASSERT_HAS(app.start_menu().message(), "Copy failed");
        writable = true;
        click(app, MenuId::Copy);
        ASSERT_TRUE(written.size() == 2 && written[1] == shown_code(app));
        ASSERT_TRUE(app.start_menu().copied());
        ASSERT_TRUE(app.start_menu().message().empty());
    } TEST_END();

    TEST_CASE("A9.2 The real clipboard of SDL (the dummy video driver's here) is what the menu uses when nothing else is set: Cmd+V pastes what SDL_SetClipboardText put there (trimmed), Copy leaves the room's code for SDL_GetClipboardText") {
        TempDir temp;
        Server server;
        Application app;
        Hall hall{&server, &app, {}, nullptr, false};
        ASSERT_TRUE(app.init(menu_config(server.address(), temp.file("s.ini"))));         // no clipboard hooks: the defaults, SDL's own
        ASSERT_EQ(SDL_SetClipboardText("  demo-tiny-2p-real \n"), 0);
        click(app, MenuId::JoinWithCode);
        click(app, MenuId::Code);
        press(app, SDLK_v, KMOD_GUI);
        ASSERT_EQ(app.start_menu().code(), std::string("demo-tiny-2p-real"));
        press(app, SDLK_ESCAPE);
        menu_host(app, "Hostess", 0, 1);
        ASSERT_TRUE(hall.until([&]() { return on_panel(app, MenuPanel::Room); }, 8000));
        ASSERT_EQ(SDL_SetClipboardText("something else"), 0);
        click(app, MenuId::Copy);
        char* text = SDL_GetClipboardText();
        ASSERT_TRUE(text != nullptr);
        const std::string copied = text != nullptr ? text : "";
        if (text != nullptr) SDL_free(text);
        ASSERT_EQ(copied, shown_code(app));
        ASSERT_TRUE(app.start_menu().copied());
        ASSERT_TRUE(app.start_menu().message().empty());
    } TEST_END();

    TEST_CASE("A10.1 The command line --start-menu --headless --screenshot FILE draws the first panel and ends (the screenshots of the menu are made this way): the picture is the menu's (the orange of the original's Single / Multi screen fills most of it), not an empty frame") {
        TempDir temp;
        // (a .bmp: the game turns a .png into a PNG with Python's PIL, which a machine need not have; the bitmap is what the renderer reads back and is the same everywhere)
        // (a command line is the 16:9 picture on a desktop now, the widescreen work: this test is about the menu's own 640 x 480 picture, so it says --aspect 4:3; A10.1b is the 16:9 one)
        std::vector<std::string> args = {"ants", "--start-menu", "--headless", "--aspect", "4:3", "--screenshot", temp.file("menu.bmp"), "--frames", "6", "--settings", temp.file("s.ini")};
        std::vector<char*> storage;
        const ApplicationConfig cfg = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, storage));
        ASSERT_TRUE(cfg.start_menu && cfg.headless && cfg.startup_error.empty());
        Application app;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_EQ(app.state(), AppState::StartMenu);
        ASSERT_EQ(app.run(), 0);
        ASSERT_EQ(app.state(), AppState::StartMenu);
        const std::string bmp = read_file(temp.file("menu.bmp"));
        ASSERT_TRUE(bmp.size() > 54 && bmp.substr(0, 2) == "BM");
        const auto le32 = [&bmp](size_t at) {
            uint32_t v = 0;
            for (size_t i = 4; i-- > 0;) v = (v << 8) | static_cast<uint8_t>(bmp[at + i]);
            return v;
        };
        const uint32_t offset = le32(10);
        const int32_t width = static_cast<int32_t>(le32(18));
        const int32_t height = std::abs(static_cast<int32_t>(le32(22)));
        const uint32_t bits = static_cast<uint8_t>(bmp[28]) | (static_cast<uint32_t>(static_cast<uint8_t>(bmp[29])) << 8);
        ASSERT_TRUE(bits == 32 && width > 0 && height > 0 && width * 3 == height * 4);        // a picture of the 4 : 3 screen (640 x 480), 32 bits a pixel
        ASSERT_TRUE(bmp.size() >= offset + static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
        size_t orange = 0;
        size_t green = 0;
        for (size_t i = 0; i < static_cast<size_t>(width) * static_cast<size_t>(height); ++i) {
            const size_t at = offset + i * 4;
            const int b = static_cast<uint8_t>(bmp[at]);
            const int g = static_cast<uint8_t>(bmp[at + 1]);
            const int r = static_cast<uint8_t>(bmp[at + 2]);
            if (r >= 190 && g >= 50 && g <= 110 && b <= 50) ++orange;
            if (r <= 70 && g >= 80 && g <= 130 && b >= 60 && b <= 110) ++green;               // the buttons' face
        }
        const size_t total = static_cast<size_t>(width) * static_cast<size_t>(height);
        ASSERT_TRUE(orange * 100 >= total * 35);                                              // the background of the Single / Multi screen
        ASSERT_TRUE(green * 100 >= total * 8);                                                // and the four buttons and the banner on it
    } TEST_END();

    TEST_CASE("A10.1b The same screenshot in the 16:9 picture (the default of a desktop): the 960 x 540 canvas with the menu's own 640 x 480 page centred at (160, 30) over the clay of the original's pages, the page pixel for pixel the 4:3 picture's") {
        TempDir temp;
        struct Shot {
            int32_t width{0};
            int32_t height{0};
            bool bottom_up{true};
            std::string bmp;
            uint32_t offset{0};
            std::array<uint8_t, 3> at(int32_t x, int32_t y) const {                           // the colour at the image's (x, y), top down
                const int32_t row = bottom_up ? height - 1 - y : y;
                const size_t i = offset + (static_cast<size_t>(row) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4;
                return {static_cast<uint8_t>(bmp[i + 2]), static_cast<uint8_t>(bmp[i + 1]), static_cast<uint8_t>(bmp[i])};
            }
        };
        const auto take = [&](const char* aspect, const char* window, const char* name) {
            std::vector<std::string> args = {"ants", "--start-menu", "--headless", "--aspect", aspect, "--window-size", window, "--screenshot", temp.file(name), "--frames", "6", "--settings", temp.file("s.ini")};
            std::vector<char*> storage;
            const ApplicationConfig cfg = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, storage));
            Shot shot;
            if (!cfg.start_menu || !cfg.startup_error.empty()) return shot;
            Application app;
            if (!app.init(cfg) || app.run() != 0) return shot;
            shot.bmp = read_file(temp.file(name));
            if (shot.bmp.size() < 54 || shot.bmp.substr(0, 2) != "BM") return shot;
            const auto le32 = [&shot](size_t at) {
                uint32_t v = 0;
                for (size_t i = 4; i-- > 0;) v = (v << 8) | static_cast<uint8_t>(shot.bmp[at + i]);
                return v;
            };
            shot.offset = le32(10);
            shot.width = static_cast<int32_t>(le32(18));
            const int32_t signed_height = static_cast<int32_t>(le32(22));
            shot.bottom_up = signed_height > 0;
            shot.height = std::abs(signed_height);
            if (shot.bmp.size() < shot.offset + static_cast<size_t>(shot.width) * static_cast<size_t>(shot.height) * 4) shot.width = shot.height = 0;
            return shot;
        };
        const Shot classic = take("4:3", "640,480", "classic.bmp");
        const Shot wide = take("16:9", "960,540", "wide.bmp");
        ASSERT_TRUE(classic.width == 640 && classic.height == 480);
        ASSERT_TRUE(wide.width == 960 && wide.height == 540);
        const std::array<uint8_t, 3> clay{219, 75, 19};
        for (const std::pair<int, int>& p : {std::pair<int, int>{0, 0}, {159, 100}, {800, 100}, {400, 10}, {400, 29}, {959, 539 - 20}, {80, 300}}) ASSERT_TRUE(wide.at(p.first, p.second) == clay);   // the clay around the page
        int64_t differ = 0;
        for (int32_t y = 0; y < 465; ++y) {                                                    // (the rows of the frame rate's plate, which is the canvas's corner, are left out)
            for (int32_t x = 0; x < 640; ++x) differ += wide.at(160 + x, 30 + y) != classic.at(x, y) ? 1 : 0;
        }
        ASSERT_EQ(differ, int64_t{0});
    } TEST_END();

    TEST_CASE("A10.2 --start-menu together with --bot SEAT:LEVEL (the way the tests and the screenshots show bots): the single-player rows start as the command line says, Continue offers exactly those bots, and a bot of another kind than the standard one is no row") {
        TempDir temp;
        write_no_quick_help(temp.file("s.ini"));
        std::vector<std::string> args = {"ants", "--start-menu", "--headless", "--bot", "1:hard", "--bot", "3:easy", "--bot", "2:idle", "--settings", temp.file("s.ini")};
        std::vector<char*> storage;
        const ApplicationConfig cfg = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, storage));
        ASSERT_TRUE(cfg.start_menu && cfg.startup_error.empty());
        ASSERT_EQ(cfg.bots.size(), size_t{3});
        Application app;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_EQ(app.state(), AppState::StartMenu);
        ASSERT_TRUE(app.start_menu().seat(1) == SeatChoice::Hard && app.start_menu().seat(3) == SeatChoice::Easy);
        ASSERT_TRUE(app.start_menu().seat(2) == SeatChoice::Empty && app.start_menu().seat(0) == SeatChoice::Empty);   // (the idle bot is the command line's own business, not a row)
        click(app, MenuId::Single);
        const std::vector<ai::BotSpec> offered = app.start_menu().bots();
        ASSERT_EQ(offered.size(), size_t{2});
        ASSERT_TRUE(offered[0].seat == 1 && offered[0].level == ai::Level::Hard && offered[1].seat == 3 && offered[1].level == ai::Level::Easy);
        ASSERT_TRUE(app.start_menu().any_bot());
    } TEST_END();

    TEST_CASE("A10.3 --start-menu together with --player 2 (the blue ants): the single-player panel offers the other three seats (green, red, black), a bot never sits at the own seat, and the match that follows is the blue player's") {
        TempDir temp;
        write_no_quick_help(temp.file("s.ini"));
        std::vector<std::string> args = {"ants", "--start-menu", "--headless", "--player", "2", "--settings", temp.file("s.ini")};
        std::vector<char*> storage;
        const ApplicationConfig cfg = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, storage));
        ASSERT_TRUE(cfg.start_menu && cfg.startup_error.empty());
        Application app;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_EQ(app.start_menu().own_seat(), 2);
        click(app, MenuId::Single);
        MenuElement row;
        ASSERT_TRUE(app.start_menu().find_element(MenuId::Seat0, row) && app.start_menu().find_element(MenuId::Seat1, row) && app.start_menu().find_element(MenuId::Seat3, row));
        ASSERT_FALSE(app.start_menu().find_element(MenuId::Seat2, row));                    // no row for the own seat
        click(app, MenuId::Seat0);                                                           // Easy at green
        click(app, MenuId::Seat3);
        click(app, MenuId::Seat3);                                                           // Medium at black
        const std::vector<ai::BotSpec> offered = app.start_menu().bots();
        ASSERT_EQ(offered.size(), size_t{2});
        ASSERT_TRUE(offered[0].seat == 0 && offered[0].level == ai::Level::Easy && offered[1].seat == 3 && offered[1].level == ai::Level::Medium);
        click(app, MenuId::Continue);
        app.pump_network(0.01f);
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_EQ(app.local_player_id(), 2);
        app.map_select().handle_key_down(SDLK_RETURN);
        ASSERT_EQ(app.state(), AppState::Playing);
        ASSERT_EQ(app.local_player_id(), 2);
        ASSERT_EQ(app.sim().roster_mask(), 0x0D);                                            // blue (bit 2), green (bit 0) and black (bit 3) play; red has no ants
        ASSERT_EQ(app.sim().get_player_name(0), std::string("Bot (Easy)"));
        ASSERT_EQ(app.sim().get_player_name(3), std::string("Bot (Medium)"));
    } TEST_END();

    TEST_CASE("A10.4 The window's own event loop reaches the menu (the tests above call the menu's handler directly): keys, typed text and the mouse queued in SDL's event queue change the panels in the next frame, and the window's close button ends the program from the menu") {
        TempDir temp;
        Application app;
        ASSERT_TRUE(app.init(menu_config("127.0.0.1:1", temp.file("s.ini"))));
        const auto push = [](SDL_Event e) { ASSERT_TRUE(SDL_PushEvent(&e) == 1); };
        app.start_menu().update(0.4f);                                                       // (the first panel has been up for a while: Enter acts)
        push(key_event(SDLK_DOWN));                                                          // first panel: Join with a code
        push(key_event(SDLK_RETURN));
        app.run_frame_with_delta(0.016f);
        ASSERT_EQ(app.state(), AppState::StartMenu);
        ASSERT_EQ(app.start_menu().panel(), MenuPanel::Join);
        push(text_event("demo-queued"));                                                     // typed text goes to the field that has the focus: the code (the name is proposed)
        app.run_frame_with_delta(0.016f);
        ASSERT_EQ(app.start_menu().code(), std::string("demo-queued"));
        app.start_menu().update(0.4f);
        push(key_event(SDLK_ESCAPE));                                                        // back to the first panel
        app.run_frame_with_delta(0.016f);
        ASSERT_EQ(app.start_menu().panel(), MenuPanel::Main);
        MenuElement host;                                                                     // the mouse: a click on "Host an online match"
        ASSERT_TRUE(app.start_menu().find_element(MenuId::HostOnline, host));
        const int32_t x = host.rect.x + host.rect.w / 2;
        const int32_t y = host.rect.y + host.rect.h / 2;
        push(mouse_event(SDL_MOUSEMOTION, x, y));
        push(mouse_event(SDL_MOUSEBUTTONDOWN, x, y));
        push(mouse_event(SDL_MOUSEBUTTONUP, x, y));
        app.run_frame_with_delta(0.016f);
        ASSERT_EQ(app.start_menu().panel(), MenuPanel::Host);
        SDL_Event quit;
        std::memset(&quit, 0, sizeof(quit));
        quit.type = SDL_QUIT;
        push(quit);
        app.run_frame_with_delta(0.016f);
        ASSERT_FALSE(app.is_running());
    } TEST_END();

    TEST_CASE("A11.1 The menu's sounds reach the mixer: the pointer over a button is silent, the press of a button plays the click (the original's buttonclick), a refusal on the Join panel plays the can't-go cue") {
        TempDir temp;
        Server server;
        Application app;
        ASSERT_TRUE(app.init(menu_config(server.address(), temp.file("s.ini"))));
        MenuElement single;
        ASSERT_TRUE(app.start_menu().find_element(MenuId::Single, single));
        const int32_t x = single.rect.x + single.rect.w / 2;
        const int32_t y = single.rect.y + single.rect.h / 2;
        const size_t base = app.audio_mixer().active_channel_count();
        app.handle_menu_event(mouse_event(SDL_MOUSEMOTION, x, y));
        ASSERT_EQ(app.audio_mixer().active_channel_count(), base);                           // the hover makes no sound
        app.handle_menu_event(mouse_event(SDL_MOUSEBUTTONDOWN, x, y));
        ASSERT_EQ(app.audio_mixer().active_channel_count(), base + 1);                       // the click at the press
        app.handle_menu_event(mouse_event(SDL_MOUSEBUTTONUP, x, y));
        ASSERT_EQ(app.start_menu().panel(), MenuPanel::Single);
        press(app, SDLK_ESCAPE);
        click(app, MenuId::JoinWithCode);
        fill(app, MenuId::Name, "Bot (Hard)");
        fill(app, MenuId::Code, "demo-tiny-2p-cue");
        const size_t before = app.audio_mixer().active_channel_count();
        press(app, SDLK_RETURN);                                                              // Enter on the code: Join, refused (a name for computer players)
        ASSERT_EQ(app.start_menu().panel(), MenuPanel::Join);
        ASSERT_EQ(app.audio_mixer().active_channel_count(), before + 1);                     // the can't-go cue
    } TEST_END();

    TEST_CASE("A12.1 NetGame tells why a join failed (fail_reason, reject_reason): none while connecting, Unreachable for a port that nothing listens on, Rejected with the server's own reason (each of the six), Lost for a connection that was open and closed before the room; leave() takes the reason back") {
        Server server(0);
        RawServer raw;
        Peer p;
        Hall hall{&server, nullptr, {&p}, &raw, false};
        using FailReason = net::NetGame::FailReason;
        ASSERT_TRUE(p.net.fail_reason() == FailReason::None);
        const uint16_t closed = []() {
            auto l = net::TcpListener::listen(0, true);
            return l ? l->port() : uint16_t{1};
        }();
        ASSERT_TRUE(p.join("127.0.0.1", closed, "Ann", "room-x"));
        ASSERT_TRUE(p.net.fail_reason() == FailReason::None);                                // not failed yet
        ASSERT_TRUE(hall.until([&]() { return p.net.phase() == net::NetGame::Phase::Failed; }, 8000));
        ASSERT_TRUE(p.net.fail_reason() == FailReason::Unreachable);
        p.net.leave();
        ASSERT_TRUE(p.net.fail_reason() == FailReason::None);                                // (leave() starts the next attempt from nothing)
        for (const net::RejectReason reason : {net::RejectReason::Full, net::RejectReason::VersionMismatch, net::RejectReason::MatchRunning, net::RejectReason::Kicked,
                                               net::RejectReason::BadRequest, net::RejectReason::NoSuchRoom, net::RejectReason::Dropped, net::RejectReason::RejoinFailed,
                                               net::RejectReason::Superseded}) {
            raw.mode = RawServer::Mode::Reject;
            raw.reason = reason;
            ASSERT_TRUE(p.join("127.0.0.1", raw.port(), "Ann", "room-x"));
            ASSERT_TRUE(hall.until([&]() { return p.net.phase() == net::NetGame::Phase::Failed; }, 8000));
            ASSERT_TRUE(p.net.fail_reason() == FailReason::Rejected);
            ASSERT_TRUE(p.net.reject_reason() == reason);
            p.net.leave();
        }
        ASSERT_TRUE(p.join("127.0.0.1", server.port(), "Ann", "no-such-room"));                // a real server: the code is no room
        ASSERT_TRUE(hall.until([&]() { return p.net.phase() == net::NetGame::Phase::Failed; }, 8000));
        ASSERT_TRUE(p.net.fail_reason() == FailReason::Rejected && p.net.reject_reason() == net::RejectReason::NoSuchRoom);
        p.net.leave();
        raw.mode = RawServer::Mode::HangUp;                                                  // the connection is open, the server closes it without a word
        ASSERT_TRUE(p.join("127.0.0.1", raw.port(), "Ann", "room-x"));
        ASSERT_TRUE(hall.until([&]() { return p.net.phase() == net::NetGame::Phase::Failed; }, 8000));
        ASSERT_TRUE(p.net.fail_reason() == FailReason::Lost);
        p.net.leave();
        ASSERT_TRUE(p.net.fail_reason() == FailReason::None && p.net.phase() == net::NetGame::Phase::Off);
    } TEST_END();


    // ---- the review fixes of the start menu ---------------------------------------------------------------------------------------------------------------------

    TEST_CASE("A13.1 The second click of a double click is not a click on the screen that the first one opened (the controls lie on top of each other): the entries and the panels, Cancel and Back, the loading screen and Quit, the quit dialog and the Host entry; a click after the double-click time is a click") {
        TempDir temp;
        Server server;
        {   // "Host an online match" and the Host panel's Host button: the second click made a room on the server
            Application app;
            Hall hall{&server, &app, {}, nullptr, false};
            ASSERT_TRUE(app.init(menu_config(server.address(), ini(temp, "a.ini"))));
            const Pt entry = middle_of(app, MenuId::HostOnline);
            queue_click(entry, 1);
            app.run_frame_with_delta(0.016f);
            ASSERT_EQ(app.start_menu().panel(), MenuPanel::Host);
            ASSERT_TRUE(lies_on(app, MenuId::Host, entry));                                 // the geometry that makes it dangerous: Host is under the entry that was clicked
            ASSERT_TRUE(app.menu_gesture_pending());
            queue_press(entry, 2);                                                           // the second click of the double click
            queue_release(entry, 2);
            app.run_frame_with_delta(0.016f);
            hall.step(200);
            ASSERT_EQ(app.start_menu().panel(), MenuPanel::Host);                           // not Connecting, not the room
            ASSERT_TRUE(app.net() == nullptr && server.mgr.list(server.now).empty());
            // the time alone is enough (SDL gave up the count: the pointer moved between the clicks): a press that is stamped no later than the double-click time after the click that changed
            // the screen, or before it (it was queued before that click was handled), is the rest of it
            ASSERT_TRUE(app.swallow_menu_gesture(1, 0));
            // the count alone is enough too, long after the first click (a slow double click): clicks 2 is the rest of a sequence, whatever the time says
            ASSERT_TRUE(app.swallow_menu_gesture(2, 0x7FFFFFFFu));
            ASSERT_TRUE(app.menu_gesture_pending());
            // a click after the double-click time is a new click: the screen's, and the rule ends
            ASSERT_FALSE(app.swallow_menu_gesture(1, SDL_GetTicks() + Application::kDoubleClickMs + 50));
            ASSERT_FALSE(app.menu_gesture_pending());
            queue_click(entry, 1);
            app.run_frame_with_delta(0.016f);
            ASSERT_TRUE(hall.until([&]() { return on_panel(app, MenuPanel::Room); }, 8000));  // (the Host button works: this is the room that it makes)
            ASSERT_EQ(server.mgr.list(server.now).size(), static_cast<size_t>(1));
        }
        {   // "Single player" and the Red seat's row: the second click changed Red to an Easy bot and wrote it to the settings
            Application app;
            ASSERT_TRUE(app.init(menu_config(server.address(), ini(temp, "b.ini"))));
            const Pt entry = middle_of(app, MenuId::Single);
            queue_click(entry, 1);
            app.run_frame_with_delta(0.016f);
            ASSERT_EQ(app.start_menu().panel(), MenuPanel::Single);
            ASSERT_TRUE(lies_on(app, MenuId::Seat1, entry));
            queue_press(entry, 2);
            queue_release(entry, 2);
            app.run_frame_with_delta(0.016f);
            ASSERT_EQ(app.start_menu().seat(1), SeatChoice::Empty);
            ASSERT_TRUE(app.start_menu().bots().empty());
            ASSERT_TRUE(read_file(temp.file("b.ini")).find("bots") == std::string::npos);   // nothing was saved
            // a cycler is clicked twice on purpose when the panel is the same: a double click on a row changes it twice (no screen changed)
            ASSERT_FALSE(app.swallow_menu_gesture(1, SDL_GetTicks() + 2 * Application::kDoubleClickMs));
            const Pt row = middle_of(app, MenuId::Seat2);
            queue_click(row, 1);
            queue_click(row, 2);
            app.run_frame_with_delta(0.016f);
            ASSERT_EQ(app.start_menu().seat(2), SeatChoice::Medium);
        }
        {   // Cancel on "Connecting" and the Join panel's Back button under it: the second click went on to the first panel; the same with the Host panel's Host button at the top of Cancel
            const auto slow = std::make_shared<Lookup>();
            for (const bool host : {false, true}) {
                Application app;
                ApplicationConfig cfg = menu_config("slow.example.org:4001", ini(temp, host ? "c2.ini" : "c1.ini"));
                cfg.host_resolver = lookup_of(slow);
                ASSERT_TRUE(app.init(cfg));
                if (host) menu_host(app, "Dave", 0, 0);
                else menu_join(app, "Dave", "abc-1");
                app.pump_network(0.01f);
                ASSERT_EQ(app.start_menu().panel(), MenuPanel::Connecting);
                MenuElement cancel;
                ASSERT_TRUE(app.start_menu().find_element(MenuId::Cancel, cancel));
                const Pt at{cancel.rect.x + cancel.rect.w / 2, cancel.rect.y + (host ? 2 : 20)};   // (the Host button ends at y 294, Cancel begins at 290)
                queue_click(at, 1);
                app.run_frame_with_delta(0.016f);
                ASSERT_EQ(app.start_menu().panel(), host ? MenuPanel::Host : MenuPanel::Join);
                ASSERT_TRUE(lies_on(app, host ? MenuId::Host : MenuId::Back, at));
                queue_press(at, 2);
                queue_release(at, 2);
                app.run_frame_with_delta(0.016f);
                ASSERT_EQ(app.start_menu().panel(), host ? MenuPanel::Host : MenuPanel::Join);   // not the first panel, not a new attempt
                ASSERT_TRUE(app.net() == nullptr);
                ASSERT_EQ(slow->calls.load(), host ? 2 : 1);                                       // (the lookups of this run so far: the Join one, then the Host one; no third)
            }
            slow->release = true;
        }
        {   // the loading screen: its closing click and the second click on "Quit" ended the program
            Application app;
            SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");                                      // a real window without a screen (as test 7.8e of the integration suite has)
            SDL_SetHint(SDL_HINT_AUDIODRIVER, "dummy");
            ApplicationConfig cfg = menu_config("127.0.0.1:1", ini(temp, "e.ini"));
            cfg.headless = false;
            cfg.skip_intro = false;
            ASSERT_TRUE(app.init(cfg));
            ASSERT_EQ(app.state(), AppState::Loading);
            StartMenu layout;
            layout.show_main();
            MenuElement quit;
            ASSERT_TRUE(layout.find_element(MenuId::Quit, quit));
            const Pt at{quit.rect.x + quit.rect.w / 2, quit.rect.y + quit.rect.h / 2};
            queue_click(at, 1);                                                               // the click that ends the loading screen
            queue_press(at, 2);
            queue_release(at, 2);
            app.run_frame_with_delta(0.016f);
            ASSERT_EQ(app.state(), AppState::StartMenu);
            ASSERT_EQ(app.start_menu().panel(), MenuPanel::Main);
            ASSERT_TRUE(app.is_running());                                                    // Quit was not pressed
            SDL_Delay(Application::kDoubleClickMs + 60);
            queue_press(at, 1);                                                               // ... a click of its own, after the sequence, is: the menu answers it
            queue_release(at, 1);
            app.run_frame_with_delta(0.016f);
            ASSERT_FALSE(app.is_running());
            app.shutdown();
        }
        {   // Continue leads to the quick help: a click that follows within the double-click time is the rest of Continue's click, not a press on the quick help's START! (and not on the setup screen's START either)
            for (const bool quick_help : {true, false}) {
                Application app;
                ASSERT_TRUE(app.init(menu_config(server.address(), quick_help ? temp.file("g.ini") : ini(temp, "h.ini"))));
                click(app, MenuId::Single);                                                       // (the menu's own handler: no gesture here)
                queue_click(middle_of(app, MenuId::Continue), 1);
                app.run_frame_with_delta(0.016f);
                ASSERT_EQ(app.state(), quick_help ? AppState::QuickHelp : AppState::MapSelect);
                const Pt start = quick_help ? Pt{578, 450} : Pt{MapSelectScreen::BTN_START_X + 49, MapSelectScreen::BTN_START_Y + 13};     // START! / START: lie under the same hand
                queue_press(start, 2);                                                             // (SDL's count says it is the second click: the rule does not depend on the machine's speed)
                queue_release(start, 2);
                app.run_frame_with_delta(0.016f);
                ASSERT_EQ(app.state(), quick_help ? AppState::QuickHelp : AppState::MapSelect);        // nothing was pressed
                SDL_Delay(Application::kDoubleClickMs + 60);
                queue_press(start, 1);                                                             // a click of its own, after the sequence
                queue_release(start, 1);
                app.run_frame_with_delta(0.016f);
                ASSERT_EQ(app.state(), quick_help ? AppState::MapSelect : AppState::Playing);
            }
        }
        {   // a match that is left through the quit dialog's Yes: the second click of the double click lands on "Host an online match" of the first panel (the Yes button lies under it)
            Application app;
            Peer ann;
            Peer bob;
            Hall hall{&server, &app, {&ann, &bob}, nullptr, false};
            ASSERT_TRUE(app.init(menu_config(server.address(), ini(temp, "f.ini"))));
            ASSERT_TRUE(ann.join("127.0.0.1", server.port(), "Ann", "demo-tiny-3p-dblclk"));
            ASSERT_TRUE(hall.until([&]() { return ann.net.phase() == net::NetGame::Phase::Room; }, 8000));
            ASSERT_TRUE(bob.join("127.0.0.1", server.port(), "Bob", "demo-tiny-3p-dblclk"));
            ASSERT_TRUE(hall.until([&]() { return bob.net.phase() == net::NetGame::Phase::Room; }, 8000));
            menu_join(app, "Dave", "demo-tiny-3p-dblclk");
            ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing; }, 20000));
            hall.step(6000);
            SDL_KeyboardEvent q_ev{};
            q_ev.type = SDL_KEYDOWN;
            q_ev.keysym.sym = SDLK_q;
            q_ev.keysym.mod = KMOD_LCTRL;
            app.handle_key_down(q_ev);
            ASSERT_TRUE(app.hud().is_quit_dialog_open());
            const Pt yes{204, 272};                                                           // the Yes button (180, 260, 49 x 24)
            queue_click(yes, 1);
            queue_press(yes, 2);
            queue_release(yes, 2);
            app.run_frame_with_delta(0.016f);
            ASSERT_EQ(app.state(), AppState::StartMenu);
            ASSERT_TRUE(lies_on(app, MenuId::HostOnline, yes));
            ASSERT_EQ(app.start_menu().panel(), MenuPanel::Main);                             // the second click did not open the Host panel
            ASSERT_TRUE(app.is_running());
        }
    } TEST_END();

    TEST_CASE("A13.2 Enter pressed twice in a hurry, through the window's own events: the first opens a panel or starts an attempt, the second does nothing (it does not make a room, cycle the map, start the game, or cancel the attempt); after the settling time Enter acts; the name that was typed is written when the program ends") {
        TempDir temp;
        Server server;
        const auto key = [](SDL_Keycode k) { queue_event(key_event(k)); };
        {   // Host an online match
            Application app;
            Hall hall{&server, &app, {}, nullptr, false};
            ASSERT_TRUE(app.init(menu_config(server.address(), ini(temp, "a.ini"))));
            app.start_menu().update(0.4f);                                                    // (the first panel has been up for a while)
            key(SDLK_DOWN);
            key(SDLK_DOWN);
            key(SDLK_RETURN);
            key(SDLK_RETURN);
            app.run_frame_with_delta(0.016f);
            hall.step(300);
            ASSERT_EQ(app.start_menu().panel(), MenuPanel::Host);
            ASSERT_TRUE(app.net() == nullptr && server.mgr.list(server.now).empty());        // no room was made
            ASSERT_EQ(app.start_menu().settings().host_map, 4);                              // and the map under the cursor (Treasure, the default) was not cycled by the second Enter
            app.start_menu().update(0.4f);
            key(SDLK_RETURN);
            app.run_frame_with_delta(0.016f);
            ASSERT_EQ(app.start_menu().settings().host_map, 5);                              // Enter of its own does cycle it (Islands)
        }
        {   // Single player
            Application app;
            ASSERT_TRUE(app.init(menu_config(server.address(), ini(temp, "b.ini"))));
            app.start_menu().update(0.4f);
            key(SDLK_RETURN);
            key(SDLK_RETURN);
            app.run_frame_with_delta(0.016f);
            ASSERT_EQ(app.state(), AppState::StartMenu);
            ASSERT_EQ(app.start_menu().panel(), MenuPanel::Single);                          // the game was not started
            ASSERT_EQ(app.start_menu().seat(1), SeatChoice::Empty);
            // Esc, Esc leaves the panel and does not leave the program
            app.start_menu().update(0.4f);
            key(SDLK_ESCAPE);
            key(SDLK_ESCAPE);
            app.run_frame_with_delta(0.016f);
            ASSERT_TRUE(app.is_running());
            ASSERT_EQ(app.start_menu().panel(), MenuPanel::Main);
        }
        {   // Join: the Enter that joins and a second one: the attempt goes on and the server answers (it used to be cancelled, silently)
            Application app;
            Hall hall{&server, &app, {}, nullptr, false};
            ASSERT_TRUE(app.init(menu_config(server.address(), ini(temp, "c.ini"))));
            app.start_menu().update(0.4f);
            key(SDLK_DOWN);
            key(SDLK_RETURN);
            app.run_frame_with_delta(0.016f);
            ASSERT_EQ(app.start_menu().panel(), MenuPanel::Join);
            queue_event(text_event("abc-1"));
            app.start_menu().update(0.4f);
            key(SDLK_RETURN);
            key(SDLK_RETURN);
            app.run_frame_with_delta(0.016f);
            ASSERT_TRUE(hall.until([&]() { return failed_on(app, MenuPanel::Join); }, 8000));
            ASSERT_HAS(app.start_menu().message(), "no room with the code abc-1");           // the server's own answer, not a cancel
        }
        {   // the name that was typed and not written yet is written when the program ends (the window's close button), not at every key
            Application app;
            ASSERT_TRUE(app.init(menu_config(server.address(), ini(temp, "d.ini"))));
            app.start_menu().update(0.4f);
            key(SDLK_DOWN);
            key(SDLK_RETURN);
            app.run_frame_with_delta(0.016f);
            key(SDLK_UP);                                                                      // the name field (its text selected)
            queue_event(text_event("Zed"));
            app.run_frame_with_delta(0.016f);
            ASSERT_EQ(app.start_menu().name(), std::string("Zed"));
            ASSERT_TRUE(read_file(temp.file("d.ini")).find("name=Zed") == std::string::npos);   // not yet: the field was not left
            SDL_Event quit;
            std::memset(&quit, 0, sizeof(quit));
            quit.type = SDL_QUIT;
            queue_event(quit);
            app.run_frame_with_delta(0.016f);
            ASSERT_FALSE(app.is_running());
            ASSERT_HAS(read_file(temp.file("d.ini")), "name=Zed");
        }
        {   // an input method's composition in progress is not text: only what it commits (SDL_TEXTINPUT) is
            Application app;
            ASSERT_TRUE(app.init(menu_config(server.address(), ini(temp, "e.ini"))));
            click(app, MenuId::JoinWithCode);
            ASSERT_EQ(app.start_menu().selected(), MenuId::Code);
            SDL_Event editing;
            std::memset(&editing, 0, sizeof(editing));
            editing.type = SDL_TEXTEDITING;
            std::snprintf(editing.edit.text, sizeof(editing.edit.text), "%s", "ni");
            editing.edit.length = 2;
            app.handle_menu_event(editing);
            ASSERT_EQ(app.start_menu().code(), std::string(""));
            app.handle_menu_event(text_event("\xE4\xBD\xA0"));                                  // the committed character: not ASCII, refused with the line
            ASSERT_EQ(app.start_menu().code(), std::string(""));
            ASSERT_EQ(app.start_menu().message(), std::string(StartMenu::kRefusedCharsText));
        }
    } TEST_END();

    TEST_CASE("A13.3 What a menu game leaves behind: Leave of a single-player game ends the program (the original's way), and a network game that ends takes everything with it: the quit dialog and the in-match windows, the attempt that was left at its room, the reason of a lost match, the seat that was played") {
        TempDir temp;
        {   // Leave of the setup screen and the quit dialog's Yes of a single-player game end the program, with the menu or without
            Server server;
            Application app;
            ASSERT_TRUE(app.init(menu_config(server.address(), ini(temp, "a.ini"))));
            click(app, MenuId::Single);
            click(app, MenuId::Continue);
            app.pump_network(0.01f);
            ASSERT_EQ(app.state(), AppState::MapSelect);
            ASSERT_FALSE(app.network_active());
            app.map_select().handle_key_down(SDLK_q);                                         // Leave
            ASSERT_FALSE(app.is_running());
        }
        {
            Server server;
            Application app;
            ASSERT_TRUE(app.init(menu_config(server.address(), ini(temp, "b.ini"))));
            click(app, MenuId::Single);
            click(app, MenuId::Continue);
            app.pump_network(0.01f);
            app.map_select().handle_key_down(SDLK_RETURN);                                    // START
            ASSERT_EQ(app.state(), AppState::Playing);
            for (int i = 0; i < 160; ++i) app.update_simulation(0.05f);                      // the "get ready" dialog is over
            SDL_KeyboardEvent q_ev{};
            q_ev.type = SDL_KEYDOWN;
            q_ev.keysym.sym = SDLK_q;
            q_ev.keysym.mod = KMOD_LCTRL;
            app.handle_key_down(q_ev);
            ASSERT_TRUE(app.hud().is_quit_dialog_open());
            SDL_KeyboardEvent y_ev{};
            y_ev.type = SDL_KEYDOWN;
            y_ev.keysym.sym = SDLK_y;
            app.handle_key_down(y_ev);
            ASSERT_FALSE(app.is_running());                                                    // (the menu is for network games: a single-player game ends the program as the original does)
        }
        {   // the room closes under a player who sits at seat 1 with the quit dialog, the match's quick help and the options open: back at the menu with all of them closed, seat 0 again
            Server server;
            Application app;
            Peer ann;
            Hall hall{&server, &app, {&ann}, nullptr, false};
            ASSERT_TRUE(ann.join("127.0.0.1", server.port(), "Ann", "demo-tiny-2p-gone01"));
            ASSERT_TRUE(hall.until([&]() { return ann.net.phase() == net::NetGame::Phase::Room; }, 8000));
            ASSERT_TRUE(app.init(menu_config(server.address(), ini(temp, "c.ini"))));
            menu_join(app, "Bob", "demo-tiny-2p-gone01");
            ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing && ann.net.phase() == net::NetGame::Phase::Playing; }, 20000));
            hall.step(1000);
            ASSERT_EQ(app.local_player_id(), 1);
            ASSERT_EQ(app.hud().local_player_id(), 1);
            app.hud().open_quit_dialog();
            app.hud().open_quick_help();
            app.hud().open_options();
            ASSERT_TRUE(app.hud().is_quit_dialog_open() && app.hud().is_quick_help_open() && app.hud().is_options_open());
            ASSERT_TRUE(server.mgr.close_room("demo-tiny-2p-gone01", server.now));
            ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::StartMenu; }, 12000));
            ASSERT_FALSE(app.hud().is_quit_dialog_open());                                    // (a dialog of the match that is gone would be on the next match's screen)
            ASSERT_FALSE(app.hud().is_quick_help_open());
            ASSERT_FALSE(app.hud().is_options_open());
            ASSERT_EQ(app.local_player_id(), 0);                                              // the seat that was played is not the seat of the next game
            ASSERT_EQ(app.hud().local_player_id(), 0);
            ASSERT_TRUE(app.net_notice().empty());                                            // the reason went to the first panel (below) and is not kept
            ASSERT_HAS(app.start_menu().message(), "lost");
        }
        {   // a room that fills while its code is on the screen starts the match; leaving it must not leave the attempt behind (the menu was told "the connection was lost" a frame later)
            Server server;
            Application app;
            Peer guest;
            Hall hall{&server, &app, {&guest}, nullptr, false};
            ASSERT_TRUE(app.init(menu_config(server.address(), ini(temp, "e.ini"))));
            menu_host(app, "Hostess", 0, 1);                                                   // Tiny, two players
            ASSERT_TRUE(hall.until([&]() { return on_panel(app, MenuPanel::Room); }, 8000));
            ASSERT_TRUE(guest.join("127.0.0.1", server.port(), "Guest", shown_code(app)));
            ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing && guest.net.phase() == net::NetGame::Phase::Playing; }, 20000));
            hall.step(1000);
            ASSERT_TRUE(server.mgr.close_room(server.mgr.list(server.now)[0].code, server.now));
            ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::StartMenu; }, 12000));
            hall.step(500);                                                                    // frames at the menu: nothing of the attempt is left to say anything
            ASSERT_EQ(app.start_menu().panel(), MenuPanel::Main);
            ASSERT_HAS(app.start_menu().message(), "lost");                                    // (the match's notice, not a failure of the attempt)
            ASSERT_TRUE(app.net() == nullptr);
        }
    } TEST_END();

    TEST_CASE("A13.4 A session that is over is not a room: the setup screen, the room's panel and a join in progress each go back to the menu with the line of their own, for Failed and for Over (Over comes only from a match that is lost, so the test puts the session there); Enter on 'Continue to the room' needs the room") {
        TempDir temp;
        Server server;
        for (const net::NetGame::Phase dead : {net::NetGame::Phase::Failed, net::NetGame::Phase::Over}) {   // under the setup screen
            Application app;
            Hall hall{&server, &app, {}, nullptr, false};
            ASSERT_TRUE(app.init(menu_config(server.address(), ini(temp, "a.ini"))));
            menu_join(app, "Dave", "demo-tiny-4p-dead02");
            ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::MapSelect && app.net() != nullptr && app.net()->phase() == net::NetGame::Phase::Room; }, 8000));
            app.net()->force_phase_for_test(dead);
            app.pump_network(0.01f);
            ASSERT_EQ(app.state(), AppState::StartMenu);
            ASSERT_TRUE(app.net() == nullptr && app.is_running());
            hall.step(100);
        }
        for (const net::NetGame::Phase dead : {net::NetGame::Phase::Failed, net::NetGame::Phase::Over}) {   // on the room's panel
            Application app;
            Hall hall{&server, &app, {}, nullptr, false};
            ASSERT_TRUE(app.init(menu_config(server.address(), ini(temp, "b.ini"))));
            menu_host(app, "Hostess", 0, 1);
            ASSERT_TRUE(hall.until([&]() { return on_panel(app, MenuPanel::Room); }, 8000));
            app.net()->force_phase_for_test(dead);
            app.pump_network(0.01f);
            ASSERT_TRUE(failed_on(app, MenuPanel::Host));
            ASSERT_TRUE(app.net() == nullptr);
            ASSERT_EQ(app.window_title(), std::string("Ants"));
        }
        {   // a join in progress whose session is over (Over, or no session at all): "lost before you were in the room", not "cannot reach"
            Application app;
            RawServer raw;
            Hall hall{nullptr, &app, {}, &raw, false};
            ASSERT_TRUE(app.init(menu_config("127.0.0.1:" + std::to_string(raw.port()), ini(temp, "c.ini"))));
            menu_join(app, "Dave", "room-x");
            ASSERT_TRUE(hall.until([&]() { return app.net() != nullptr && app.net()->phase() == net::NetGame::Phase::Connecting && raw.accepted == 1; }, 8000));
            app.net()->force_phase_for_test(net::NetGame::Phase::Over);
            app.pump_network(0.01f);
            ASSERT_TRUE(failed_on(app, MenuPanel::Join));
            ASSERT_HAS(app.start_menu().message(), "was lost before you were in the room");
            ASSERT_TRUE(app.start_menu().message().find("Cannot reach") == std::string::npos);
            ASSERT_TRUE(app.net() == nullptr);
        }
        {   // "Continue to the room" with the match already loading: the room is no longer the screen to go to
            Application app;
            Hall hall{&server, &app, {}, nullptr, false};
            ASSERT_TRUE(app.init(menu_config(server.address(), ini(temp, "d.ini"))));
            menu_host(app, "Hostess", 0, 1);
            ASSERT_TRUE(hall.until([&]() { return on_panel(app, MenuPanel::Room); }, 8000));
            app.net()->force_phase_for_test(net::NetGame::Phase::Loading);
            click(app, MenuId::EnterRoom);
            app.pump_network(0.0f);
            ASSERT_EQ(app.state(), AppState::StartMenu);                                      // nothing happened
            ASSERT_EQ(app.start_menu().panel(), MenuPanel::Room);
            app.net()->force_phase_for_test(net::NetGame::Phase::Room);
            click(app, MenuId::EnterRoom);
            app.pump_network(0.0f);
            ASSERT_EQ(app.state(), AppState::MapSelect);                                      // with the room it does
        }
    } TEST_END();

    TEST_CASE("A13.5 Time limits and silence: the player's limit is 20 seconds for a lookup that never ends, a server that accepts and never says Welcome ends after the room's 10 seconds; both say 'The server <name> did not answer' (one rule), a server that hangs up says 'lost before you were in the room', and NetGame names the difference (NoAnswer, Lost)") {
        TempDir temp;
        ASSERT_EQ(ApplicationConfig{}.menu_connect_timeout_ms, 20000u);                       // the production limit is the default of the config (the tests below do not set it)
        {   // a name that is never resolved: 20 s of game time, not 5 and not 60
            Application app;
            const auto lookup = std::make_shared<Lookup>();
            Hall hall{nullptr, &app, {}, nullptr, false};
            ApplicationConfig cfg = menu_config("never.example.org:4001", ini(temp, "a.ini"));
            cfg.host_resolver = lookup_of(lookup);
            ASSERT_TRUE(app.init(cfg));
            menu_join(app, "Dave", "abc-1");
            uint32_t waited = 0;
            while (waited < 30000 && !failed_on(app, MenuPanel::Join)) {
                hall.step(10);
                waited += 10;
            }
            ASSERT_TRUE(waited >= 19900 && waited <= 20300);
            ASSERT_HAS(app.start_menu().message(), "The server never.example.org:4001 did not answer");
            lookup->release = true;
        }
        {   // a server that accepts and stays silent: the lobby's welcome limit (10 s) ends it, and the line is the same
            Application app;
            RawServer raw;
            raw.mode = RawServer::Mode::Silent;
            Hall hall{nullptr, &app, {}, &raw, false};
            ASSERT_TRUE(app.init(menu_config("127.0.0.1:" + std::to_string(raw.port()), ini(temp, "b.ini"))));
            menu_join(app, "Dave", "abc-1");
            uint32_t waited = 0;
            while (waited < 30000 && !failed_on(app, MenuPanel::Join)) {
                hall.step(10);
                waited += 10;
            }
            ASSERT_TRUE(waited >= 9900 && waited <= 14000);                                    // (the game's clock runs ahead of the socket: the connection takes some of it)
            ASSERT_HAS(app.start_menu().message(), "did not answer");
            ASSERT_TRUE(app.start_menu().message().find("lost") == std::string::npos);
            ASSERT_EQ(raw.hellos.size(), static_cast<size_t>(1));                             // the guest asked for the room and named itself, for any seat (255)
            ASSERT_TRUE(raw.hellos[0].want_seat == 255 && raw.hellos[0].name == "Dave" && raw.hellos[0].room == "abc-1");
        }
        {   // NetGame alone: the silent server is NoAnswer after ten seconds of the guest's clock; the one that hangs up is Lost at once
            RawServer raw;
            Peer p;
            Hall hall{nullptr, nullptr, {&p}, &raw, false};
            using FailReason = net::NetGame::FailReason;
            raw.mode = RawServer::Mode::Silent;
            ASSERT_TRUE(p.join("127.0.0.1", raw.port(), "Ann", "room-x"));
            const uint32_t begun = p.now;
            ASSERT_TRUE(hall.until([&]() { return p.net.phase() == net::NetGame::Phase::Failed; }, 20000));
            ASSERT_TRUE(p.net.fail_reason() == FailReason::NoAnswer);
            ASSERT_TRUE(p.now - begun >= 9900 && p.now - begun <= 14000);
            p.net.leave();
            raw.mode = RawServer::Mode::HangUp;
            ASSERT_TRUE(p.join("127.0.0.1", raw.port(), "Ann", "room-x"));
            ASSERT_TRUE(hall.until([&]() { return p.net.phase() == net::NetGame::Phase::Failed; }, 20000));
            ASSERT_TRUE(p.net.fail_reason() == FailReason::Lost);
        }
        {   // the player's limit also covers a server that has accepted and says nothing (here a limit of 3 s, shorter than the room's welcome): the attempt ends at the player's limit
            Application app;
            RawServer raw;
            raw.mode = RawServer::Mode::Silent;
            Hall hall{nullptr, &app, {}, &raw, false};
            ApplicationConfig cfg = menu_config("127.0.0.1:" + std::to_string(raw.port()), ini(temp, "b3.ini"));
            cfg.menu_connect_timeout_ms = 3000;
            ASSERT_TRUE(app.init(cfg));
            menu_join(app, "Dave", "abc-1");
            uint32_t waited = 0;
            while (waited < 30000 && !failed_on(app, MenuPanel::Join)) {
                hall.step(10);
                waited += 10;
            }
            ASSERT_TRUE(waited >= 2900 && waited <= 8000);                                     // not the room's 10 s
            ASSERT_HAS(app.start_menu().message(), "did not answer");
            ASSERT_TRUE(app.net() == nullptr);
        }
        {   // a server that hangs up before the Welcome: the line of a lost connection
            Application app;
            RawServer raw;
            raw.mode = RawServer::Mode::HangUp;
            Hall hall{nullptr, &app, {}, &raw, false};
            ASSERT_TRUE(app.init(menu_config("127.0.0.1:" + std::to_string(raw.port()), ini(temp, "c.ini"))));
            menu_join(app, "Dave", "abc-1");
            ASSERT_TRUE(hall.until([&]() { return failed_on(app, MenuPanel::Join); }, 8000));
            ASSERT_HAS(app.start_menu().message(), "was lost before you were in the room");
        }
    } TEST_END();

    TEST_CASE("A13.6 A lookup that cannot start its thread says so (not 'the name is not known'), the player of a bot seat at the setup screen is named before START, and a server that does not offer the map that the player chose is told, and the room is left") {
        TempDir temp;
        {   // the system has no thread to give
            Application app;
            ApplicationConfig cfg = menu_config("somewhere.example.org:4001", ini(temp, "a.ini"));
            cfg.host_resolver = [](const std::string&, std::string&, std::string&) { return false; };
            cfg.host_launcher = [](std::function<void()>) { throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again)); };
            ASSERT_TRUE(app.init(cfg));
            menu_join(app, "Dave", "abc-1");
            app.pump_network(0.01f);
            ASSERT_TRUE(failed_on(app, MenuPanel::Join));
            ASSERT_HAS(app.start_menu().message(), "Could not start the lookup of somewhere.example.org");
            ASSERT_TRUE(app.start_menu().message().find("not known") == std::string::npos);
            ASSERT_TRUE(app.net() == nullptr && app.is_running());
        }
        {   // Continue with two bots: the setup screen names them before START (the original's single-player game keeps its names)
            Server server;
            Application app;
            ApplicationConfig cfg = menu_config(server.address(), ini(temp, "b.ini"));
            cfg.team_names[1] = "Zed";                                                         // (--team-name: a name that the command line gave to a seat)
            ASSERT_TRUE(app.init(cfg));
            click(app, MenuId::Single);
            click(app, MenuId::Seat2);                                                         // Blue: Easy
            click(app, MenuId::Seat3);
            click(app, MenuId::Seat3);
            click(app, MenuId::Seat3);                                                         // Black: Hard
            click(app, MenuId::Continue);
            app.pump_network(0.01f);
            ASSERT_EQ(app.state(), AppState::MapSelect);                                       // (before START)
            ASSERT_EQ(app.sim().get_player_name(2), std::string("Bot (Easy)"));
            ASSERT_EQ(app.sim().get_player_name(3), std::string("Bot (Hard)"));
            ASSERT_EQ(app.sim().get_player_name(1), std::string("Zed"));                       // a seat without a bot keeps what the command line named it
        }
        {   // a server whose demo rooms are on Tiny and Small only, and the player asked for Islands (the menu offers the six maps of the game)
            Server server(4, {"TINY.LVL", "SMALL.LVL"});
            Application app;
            Hall hall{&server, &app, {}, nullptr, false};
            ASSERT_TRUE(app.init(menu_config(server.address(), ini(temp, "c.ini"))));
            menu_host(app, "Hostess", 5, 2);                                                   // Islands, three players
            ASSERT_TRUE(hall.until([&]() { return failed_on(app, MenuPanel::Host); }, 8000));
            ASSERT_HAS(app.start_menu().message(), "does not offer the Islands map");
            ASSERT_HAS(app.start_menu().message(), "TINY.LVL");                               // what the server would have played instead
            ASSERT_TRUE(app.net() == nullptr);
            ASSERT_EQ(app.window_title(), std::string("Ants"));
            ASSERT_EQ(app.start_menu().settings().host_map, 5);                               // the choice stays: the player picks another map and goes on
            ASSERT_TRUE(hall.until([&]() { return server.mgr.list(server.now).empty() || server.mgr.list(server.now)[0].joined == 0; }, 4000));   // (the player left the room again)
            click(app, MenuId::HostMap);                                                       // Islands -> Tiny
            click(app, MenuId::Host);
            ASSERT_TRUE(hall.until([&]() { return on_panel(app, MenuPanel::Room); }, 8000));  // a map that the server offers is made
            click(app, MenuId::Back);
            hall.step(100);
            click(app, MenuId::HostMap);                                                       // Tiny -> Small: offered too
            click(app, MenuId::Host);
            ASSERT_TRUE(hall.until([&]() { return on_panel(app, MenuPanel::Room); }, 8000));
        }
    } TEST_END();

    TEST_CASE("A14.1 Host an online match with \"Empty seats at START\" = Medium bots: the choice reaches the application and the room's network before the connection is made, the room's panel says what START will do, the leader's screen says it on the status line, one person alone starts the match with START and the server seats three \"Bot (Medium)\" (they play), and the choice is remembered for the next run") {
        TempDir temp;
        const std::string settings = ini(temp, "s.ini");
        Server server;
        std::string code;
        {
            Application app;
            Hall hall{&server, &app, {}, nullptr, false};
            ASSERT_TRUE(app.init(menu_config(server.address(), settings)));
            ASSERT_EQ(app.fill_bots(), net::FillLevel::None);                                  // nothing is seated unless the player chose it
            click(app, MenuId::HostOnline);
            click(app, MenuId::HostFill);                                                      // Leave empty -> Easy bots
            click(app, MenuId::HostFill);                                                      // -> Medium bots
            fill(app, MenuId::HostName, "Solo");
            click(app, MenuId::Host);
            ASSERT_TRUE(hall.until([&]() { return on_panel(app, MenuPanel::Room); }, 8000));
            code = shown_code(app);
            ASSERT_TRUE(app.fill_bots() == net::FillLevel::Medium && app.net() != nullptr && app.net()->fill_bots() == net::FillLevel::Medium);
            bool says = false;
            for (const MenuElement& e : app.start_menu().elements()) says = says || e.text == "Empty seats will be Medium bots.";
            ASSERT_TRUE(says);                                                                 // the room's panel: what START will do
            click(app, MenuId::EnterRoom);
            hall.step(700);
            ASSERT_EQ(app.state(), AppState::MapSelect);
            ASSERT_TRUE(app.map_select().leads_server_room());
            ASSERT_EQ(app.map_select().room().status, std::string("Press START: the empty seats get Medium bots."));
            const size_t channels = app.audio_mixer().active_channel_count();
            app.room_key_down(SDLK_s, 0, false);                                               // START, alone: one person is enough with a fill
            ASSERT_EQ(app.audio_mixer().active_channel_count(), channels);                     // (no can't-go cue)
            ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing; }, 20000));
            const server::RoomStatus s = server.status(code);
            ASSERT_TRUE(s.state == server::RoomState::Running && s.joined == 4 && s.expected == 4 && s.bots.size() == 3);
            ASSERT_TRUE(s.names[0] == "Solo" && s.names[1] == "Bot (Medium)" && s.names[2] == "Bot (Medium)" && s.names[3] == "Bot (Medium)");
            ASSERT_TRUE(s.bots[0].fill && s.bots[0].level == "medium" && s.bots[0].kind == "standard");
            ASSERT_EQ(app.sim().roster_mask(), 0x0F);
            hall.step(4000);
            ASSERT_FALSE(app.net()->desynced());
            ASSERT_TRUE(server.status(code).ticks > 40);
        }
        ASSERT_HAS(read_file(settings), "host_fill=medium\n");                                  // written when the choice changed
        {   // the next run: the Host panel shows it
            Application app;
            ASSERT_TRUE(app.init(menu_config(server.address(), settings)));
            ASSERT_EQ(app.start_menu().settings().host_fill, net::FillLevel::Medium);
            click(app, MenuId::HostOnline);
            MenuElement e;
            ASSERT_TRUE(app.start_menu().find_element(MenuId::HostFill, e) && e.value == "Medium bots");
        }
    } TEST_END();

    TEST_CASE("A14.2 The fill belongs to the Host panel: --start-menu --fill-bots hard makes the panel start at Hard bots (nothing is written until the player changes it); a player who JOINS fills nothing, whatever the panel said before (only the leader's START seats bots, and the choice is its host's)") {
        TempDir temp;
        const std::string settings = ini(temp, "s.ini");
        {
            std::vector<std::string> args = {"ants", "--headless", "--start-menu", "--fill-bots", "hard", "--settings", settings};
            std::vector<char*> storage;
            const ApplicationConfig cfg = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, storage));
            ASSERT_TRUE(cfg.startup_error.empty() && cfg.start_menu && cfg.fill_bots == net::FillLevel::Hard);
            Application app;
            ASSERT_TRUE(app.init(cfg));
            ASSERT_EQ(app.start_menu().settings().host_fill, net::FillLevel::Hard);
            click(app, MenuId::HostOnline);
            MenuElement e;
            ASSERT_TRUE(app.start_menu().find_element(MenuId::HostFill, e) && e.value == "Hard bots");
        }
        ASSERT_TRUE(read_file(settings).find("host_fill") == std::string::npos);
        Server server;
        ASSERT_TRUE(server.make_room("JOIN-FILL", 4));
        Application app;
        Hall hall{&server, &app, {}, nullptr, false};
        ASSERT_TRUE(app.init(menu_config(server.address(), settings)));
        click(app, MenuId::HostOnline);
        click(app, MenuId::HostFill);
        click(app, MenuId::HostFill);
        click(app, MenuId::HostFill);                                                          // Hard bots on the Host panel
        ASSERT_EQ(app.start_menu().settings().host_fill, net::FillLevel::Hard);
        click(app, MenuId::Host);                                                              // a room is made with it: the application's fill is Hard now
        ASSERT_TRUE(hall.until([&]() { return on_panel(app, MenuPanel::Room); }, 8000));
        ASSERT_TRUE(app.fill_bots() == net::FillLevel::Hard && app.net() != nullptr && app.net()->fill_bots() == net::FillLevel::Hard);
        click(app, MenuId::Back);                                                              // the room is left again
        ASSERT_TRUE(hall.until([&]() { return on_panel(app, MenuPanel::Host) && app.net() == nullptr; }, 8000));
        press(app, SDLK_ESCAPE);
        menu_join(app, "Joiner", "JOIN-FILL");                                                 // ... and then a join, the first player of a room: its leader
        ASSERT_TRUE(hall.until([&]() { return app.net() != nullptr && app.net()->phase() == net::NetGame::Phase::Room && app.net()->is_leader(); }, 8000));
        ASSERT_TRUE(app.fill_bots() == net::FillLevel::None && app.net()->fill_bots() == net::FillLevel::None);
        to_setup_screen(app);
        hall.step(700);
        ASSERT_EQ(app.map_select().room().status, std::string("Press START when all players' thumbs have appeared."));
        const size_t channels = app.audio_mixer().active_channel_count();
        app.room_key_down(SDLK_s, 0, false);                                                   // alone and without a fill: the can't-go cue, as ever
        ASSERT_EQ(app.audio_mixer().active_channel_count(), channels + 1);
        hall.step(1000);
        ASSERT_TRUE(server.status("JOIN-FILL").state == server::RoomState::Waiting && server.status("JOIN-FILL").bots.empty());
    } TEST_END();

    TEST_CASE("A14.3 Leave Game Takes The 16:9 Setup Screen's Chat Box And Fill Footer With It: A Leader In The Room (Host Panel, Medium Bots, A Line Typed) Presses Q, Is Back At The Menu, And The Next Screen Of This Run Has No Chat Panel, No Typed Line And No Footer; The Next Room Starts With An Empty Box") {
        TempDir temp;
        const std::string settings = ini(temp, "s.ini");
        Server server;
        Application app;
        Hall hall{&server, &app, {}, nullptr, false};
        ApplicationConfig cfg = menu_config(server.address(), settings);
        cfg.aspect = Aspect::Wide16x9;
        cfg.aspect_given = true;
        ASSERT_TRUE(app.init(cfg));
        for (int round = 0; round < 2; ++round) {
            click(app, MenuId::HostOnline);
            if (round == 0) {
                click(app, MenuId::HostFill);                                                  // Leave empty -> Easy bots
                click(app, MenuId::HostFill);                                                  // -> Medium bots
                fill(app, MenuId::HostName, "Solo");
            }
            click(app, MenuId::Host);
            ASSERT_TRUE(hall.until([&]() { return on_panel(app, MenuPanel::Room); }, 8000));
            click(app, MenuId::EnterRoom);
            hall.step(700);
            ASSERT_TRUE(app.state() == AppState::MapSelect && app.map_select().leads_server_room() && app.map_select().wide_layout());
            ASSERT_TRUE(app.map_select().chat_panel().visible && app.map_select().chat_panel().lines.empty());     // (the second room starts with an empty box)
            ASSERT_EQ(app.map_select().fill_footer()[1], std::string("Medium bots"));
            app.room_key_down(SDLK_t, 0, false);
            app.room_text_input("t");
            app.room_text_input("half a line");
            hall.step(30);
            ASSERT_EQ(app.map_select().chat_panel().typed, std::string("half a line"));
            app.room_key_down(SDLK_ESCAPE, 0, false);
            ASSERT_TRUE(app.net()->chat("only the leader here"));
            ASSERT_TRUE(hall.until([&]() { return app.map_select().chat_panel().lines.size() == 1; }, 8000));
            hall.step(500);
            app.room_key_down(SDLK_q, 0, false);                                               // Leave Game: back at the menu, the room is gone
            ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::StartMenu && app.net() == nullptr; }, 8000));
            ASSERT_FALSE(app.map_select().chat_panel().visible);
            ASSERT_TRUE(app.map_select().chat_panel().lines.empty() && app.map_select().chat_panel().typed.empty() && !app.map_select().chat_panel().caret);
            ASSERT_TRUE(app.map_select().fill_footer()[0].empty() && app.map_select().fill_footer()[1].empty());
            ASSERT_FALSE(app.room_chat().is_open());
            ASSERT_TRUE(app.room_chat_box() == nullptr);
        }
    } TEST_END();

    std::cout << "\nstart menu in the application: " << g_test_count << " tests, " << g_assert_count << " assertions, " << g_test_failures << " failures\n";
    if (g_test_count == 0) {                                      // (a misspelt or forgotten filter must not turn the suite green)
        std::cout << "\n no test ran: the filter ANTS_TEST_FILTER matches no test of this suite\n";
        return 1;
    }
    return g_test_failures == 0 ? 0 : 1;
}
