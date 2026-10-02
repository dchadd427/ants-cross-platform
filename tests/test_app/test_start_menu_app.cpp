// The desktop start menu in the application (include/ants_app/start_menu.hpp, src/ants_app/application_menu.cpp): the whole of what a player does with it, against a REAL
// RoomManager of the dedicated server behind a real TCP listener on the loopback interface (as test_network_app does for the leader's START): single player with no bot (the
// original game exactly) and with bots (seated as --bot seats them, fog off), join a demo room, host a room (the code, the leader's screen, a second client, START), every way
// a join can fail, cancel, and the way back to the menu after a network game. One Application per test (SDL is initialised once per process).
//
//   test_start_menu_app                         runs the tests
//   test_start_menu_app --shots DIR             writes the screenshots of every panel and state of the menu as DIR/*.png (they are what the menu looks like; nothing is compared)
//   test_start_menu_app --real-server H:P       runs the host / join / START scenario against a game server that is already running (the gate "a real server": a native ants_server
//                                               on localhost started with --demo-rooms 4 --demo-map TINY.LVL ...), then exits
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

using namespace ants;
using namespace ants::app;

#ifndef ORIGINAL_ASSETS_DIR
#define ORIGINAL_ASSETS_DIR "Original-Ants"
#endif

namespace fs = std::filesystem;

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
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

server::ServerLimits demo_limits(size_t demo_rooms) {
    server::ServerLimits limits;
    limits.demo_rooms = demo_rooms;
    limits.demo_map = "TINY.LVL";
    limits.demo_maps = {"TINY.LVL", "SMALL.LVL", "MEDIUM.LVL", "GAUNTLET.LVL", "TREASURE.LVL", "ISLANDS.LVL"};
    return limits;
}

struct Server {
    server::RoomManager mgr;
    std::unique_ptr<net::TcpListener> listener{net::TcpListener::listen(0, true)};
    uint32_t now{1000};

    explicit Server(size_t demo_rooms = 4) : mgr(server::MapStore(std::string(ORIGINAL_ASSETS_DIR) + "/Maps"), demo_limits(demo_rooms)) {}
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

void press(Application& app, SDL_Keycode sym, uint16_t mod = 0) { app.handle_menu_event(key_event(sym, mod)); }
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

// "Host an online match" from the first panel: the map (a click goes forward through the six), the players (the page's default is 4: a click gives 2, two clicks 3), the name, Host
void menu_host(Application& app, const std::string& name, int map_clicks, int players_clicks) {
    click(app, MenuId::HostOnline);
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

void click_fog(Application& app, bool on) {
    const int32_t x = (on ? MapSelectScreen::BTN_FOW_ON_X : MapSelectScreen::BTN_FOW_OFF_X) + 2;
    const int32_t y = (on ? MapSelectScreen::BTN_FOW_ON_Y : MapSelectScreen::BTN_FOW_OFF_Y) + 2;
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

// What a match looks like when it has just started and has run for 100 ticks: what two ways into the same game must agree on
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
    for (int i = 0; i < 100; ++i) app.update_simulation(0.05f);
    p.hash = app.sim().state_hash().total;
    if (p.bots) {
        p.decisions1 = app.bots()->stats(1).decisions;
        p.decisions3 = app.bots()->stats(3).decisions;
    }
    return p;
}

bool same(const Played& a, const Played& b) {
    return a.roster == b.roster && a.hash == b.hash && a.ants == b.ants && a.bots == b.bots && a.bot_seats == b.bot_seats && a.fog == b.fog && a.names == b.names && a.seat == b.seat &&
           a.decisions1 == b.decisions1 && a.decisions3 == b.decisions3 && a.state == b.state;
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
            click(app, MenuId::HostMap);                                                     // small
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

    TEST_CASE("A2.1 Single player with nobody: the match is the original's single-player game exactly: the same four teams, no bot code, the same state at tick 100 as the game that never saw the menu") {
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

    TEST_CASE("A2.2 Single player with two bots: the match starts with exactly the bots that --bot 1:medium --bot 3:hard gives (same roster, names, bot seats, state at tick 100, decisions); the fog option refuses START with the reason, without fog it starts") {
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

    TEST_CASE("A5.2 Every reason that a server can answer with is told in its own words: room full, another version (this game's version named), the match already running, removed, bad request, no such room; hosting that is refused by NoSuchRoom says the server is busy") {
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
        ASSERT_TRUE(hall.until([&]() { return failed_on(app, MenuPanel::Join) && app.start_menu().message().find("does not answer") != std::string::npos; }, 8000));
        ASSERT_HAS(app.start_menu().message(), "slow.example.org:" + std::to_string(raw.port()) + " does not answer");
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
        ASSERT_HAS(app.start_menu().message(), "does not answer");
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
        ASSERT_HAS(app.start_menu().message(), "could not be copied");
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
        std::vector<std::string> args = {"ants", "--start-menu", "--headless", "--screenshot", temp.file("menu.bmp"), "--frames", "6", "--settings", temp.file("s.ini")};
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
        push(key_event(SDLK_DOWN));                                                          // first panel: Join with a code
        push(key_event(SDLK_RETURN));
        app.run_frame_with_delta(0.016f);
        ASSERT_EQ(app.state(), AppState::StartMenu);
        ASSERT_EQ(app.start_menu().panel(), MenuPanel::Join);
        push(text_event("demo-queued"));                                                     // typed text goes to the field that has the focus: the code (the name is proposed)
        app.run_frame_with_delta(0.016f);
        ASSERT_EQ(app.start_menu().code(), std::string("demo-queued"));
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
                                               net::RejectReason::BadRequest, net::RejectReason::NoSuchRoom}) {
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

    std::cout << "\nstart menu in the application: " << g_test_count << " tests, " << g_assert_count << " assertions, " << g_test_failures << " failures\n";
    return g_test_failures == 0 ? 0 : 1;
}
