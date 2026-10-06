// Tests of the network in the application: the command line (names, --host, --join), a headless Application as the host of a room and as a guest of
// one (the other side is a bare NetGame with its own simulation), the setup screen as the room with names and thumbs, the start, a match driven by the
// lock-step runner with commands and chat, a guest that leaves, a host that leaves, a hidden page (the web build: no frames, the server's messages wake the
// game: N5.32 - N5.39). Real sockets on the loopback interface; one Application per test (SDL is initialised once per process).
#include "ants_ai/bot_view.hpp"
#include "ants_app/application.hpp"
#include "ants_app/fps_overlay.hpp"
#include "ants_app/lan_list.hpp"
#include "ants_app/latency_corner.hpp"
#include "ants_app/text_layout.hpp"
#include "ants_app/version.hpp"
#include "ants_net/lan.hpp"
#include "ants_net/netgame.hpp"
#include "ants_net/protocol.hpp"
#include "ants_net/tcp.hpp"
#include "ants_server/map_store.hpp"
#include "ants_server/room.hpp"
#include "ants_server/room_manager.hpp"
#include "ants_sim/game_strings.hpp"
#include "ants_sim/movement_tables.hpp"
#include "ants_sim/sim_engine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include "ants_test_paths.hpp"

using namespace ants;
using namespace ants::app;
using ants::sim::Command;
using ants::sim::CommandType;

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
    // ANTS_TEST_FILTER=text runs only the cases whose title contains the text (for working on one test and for mutation runs; the suite as run_tests.sh runs it has no filter)
    if (const char* filter = std::getenv("ANTS_TEST_FILTER")) {
        if (name.find(filter) == std::string::npos) return;
    }
    ++g_test_count;
    std::cout << "  RUNNING: " << std::left << std::setw(100) << name << " ... " << std::flush;
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

namespace {

std::string maps_dir() { return std::string(ORIGINAL_ASSETS_DIR) + "/Maps/"; }

// The "Get ready to play!" dialog that opens every match, and what the simulation does meanwhile: nothing (the match clock waits for it). A local game counts the dialog in kDialogSteps steps of 50 ms
// of its frame clock (HUD::kMatchStartModalSteps); the host of a match of the network seals the first turn kDialogMs after the match began (net::kMatchStartDelayMs, protocol 12), so a machine's
// first tick comes that long after its Begin.
constexpr int kDialogSteps = static_cast<int>(HUD::kMatchStartModalSteps);
constexpr uint32_t kDialogMs = net::kMatchStartDelayMs;

// The other machine of a test: a simulation and a NetGame, with the little that the application does for the room (load the map, report)
struct Peer {
    sim::SimulationEngine sim;
    net::NetGame net{sim};
    std::vector<net::ChatMsg> chats;
    std::vector<net::NetGame::Event> events;
    uint32_t now{1000};
    bool check_hash{true};                  // false: this machine accepts the host's map file whatever its hash says
    bool hold_report{false};                // true: the map is loaded but the report waits for release_report() (the room stays in its loading phase)
    bool report_pending{false};
    bool pending_ok{false};

    Peer() {
        net.set_discovery(0);                                              // the tests do not announce on the real network
        net.set_on_chat([this](const net::ChatMsg& c) { chats.push_back(c); });
    }
    void update() {
        net.update(now);
        for (const auto& ev : net.take_events()) {
            events.push_back(ev);
            if (ev.type == net::NetGame::Event::Type::StartRequested) {
                const net::StartMsg& s = net.start_info();
                ants::assets::LevelData level;
                uint64_t hash = 0;
                const bool ok = level.load_lvl(maps_dir() + s.map_name) && net::hash_file(maps_dir() + s.map_name, hash) && (!check_hash || hash == s.map_hash);
                if (ok) {
                    sim.set_fog_of_war_enabled(s.fog);
                    sim.init(level, s.seed, s.roster);
                    sim::apply_start_teams(sim, s.teams());
                }
                if (hold_report) {
                    report_pending = true;
                    pending_ok = ok;
                } else {
                    net.report_loaded(ok);
                }
            }
        }
    }
    void release_report() {
        if (!report_pending) return;
        report_pending = false;
        net.report_loaded(pending_ok);
    }
    bool saw(net::NetGame::Event::Type t) const {
        for (const auto& e : events) {
            if (e.type == t) return true;
        }
        return false;
    }
};

// Steps the application and the peer together in 10 ms of game time
struct Duo {
    Application& app;
    Peer& peer;
    void step(uint32_t ms) {
        for (uint32_t t = 0; t < ms; t += 10) {
            app.pump_network(0.010f);
            app.update_simulation(0.010f);
            peer.now += 10;
            peer.update();
            std::this_thread::sleep_for(std::chrono::microseconds(300));
        }
    }
    bool until(const std::function<bool()>& cond, uint32_t max_ms) {
        for (uint32_t t = 0; t < max_ms; t += 10) {
            if (cond()) return true;
            step(10);
        }
        // the game clock is virtual but the sockets are real: on a busy machine the kernel can be late, so it gets up to two more seconds of real
        // time with the game clock standing still (nothing times out meanwhile); a wait that succeeds never gets here
        for (int i = 0; i < 2000 && !cond(); ++i) {
            app.pump_network(0.0f);
            peer.update();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return cond();
    }
};

// The application and two bare machines, stepped together in 10 ms of game time
struct Trio {
    Application& app;
    Peer& first;
    Peer& second;
    void step(uint32_t ms) {
        for (uint32_t t = 0; t < ms; t += 10) {
            app.pump_network(0.010f);
            app.update_simulation(0.010f);
            first.now += 10;
            first.update();
            second.now += 10;
            second.update();
            std::this_thread::sleep_for(std::chrono::microseconds(300));
        }
    }
    bool until(const std::function<bool()>& cond, uint32_t max_ms) {
        for (uint32_t t = 0; t < max_ms; t += 10) {
            if (cond()) return true;
            step(10);
        }
        for (int i = 0; i < 2000 && !cond(); ++i) {       // real time for a late kernel, game clock standing still (see Duo)
            app.pump_network(0.0f);
            first.update();
            second.update();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return cond();
    }
};

// A dedicated server in the test: the real room manager behind a real TCP listener on the loopback interface. The tests step it together with the application and
// the bare machines (a room is made with make_room, joined with the room's code; the first player who joins leads it)
struct Server {
    server::RoomManager mgr;
    std::unique_ptr<net::TcpListener> listener{net::TcpListener::listen(0, true)};
    uint32_t now{1000};

    explicit Server(const server::ServerLimits& limits = server::ServerLimits()) : mgr(server::MapStore(std::string(ORIGINAL_ASSETS_DIR) + "/Maps"), limits) {}

    bool make_room(const std::string& code, uint8_t players, bool early_start = true) {
        server::RoomSpec spec;
        spec.code = code;
        spec.map = "TINY.LVL";
        spec.players = players;
        spec.early_start = early_start;
        spec.has_seed = true;
        spec.seed = 4242;
        return listener != nullptr && mgr.create_room(spec, now).ok;
    }
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
};

// The application, bare machines and a server, stepped together in 10 ms of game time
struct Hall {
    Server& server;
    Application* app{nullptr};
    std::vector<Peer*> peers;
    Application* second{nullptr};           // a second application (a guest that is a whole game: its HUD tells the News Flash)
    void step(uint32_t ms) {
        for (uint32_t t = 0; t < ms; t += 10) {
            if (app != nullptr) {
                app->pump_network(0.010f);
                app->update_simulation(0.010f);
            }
            if (second != nullptr) {
                second->pump_network(0.010f);
                second->update_simulation(0.010f);
            }
            for (Peer* p : peers) {
                p->now += 10;
                p->update();
            }
            server.now += 10;
            server.update();
            std::this_thread::sleep_for(std::chrono::microseconds(300));
        }
    }
    bool until(const std::function<bool()>& cond, uint32_t max_ms) {
        for (uint32_t t = 0; t < max_ms; t += 10) {
            if (cond()) return true;
            step(10);
        }
        for (int i = 0; i < 2000 && !cond(); ++i) {       // real time for a late kernel, game clock standing still (see Duo)
            if (app != nullptr) app->pump_network(0.0f);
            if (second != nullptr) second->pump_network(0.0f);
            for (Peer* p : peers) p->update();
            server.update();
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

// A three-player match on SMALL.LVL: the host (Alice, seat 0) and Bob (seat 1) are bare machines, the application is seat 2 (named `app_name`)
bool start_three(Peer& host, Peer& bob, Application& app, const std::string& app_name);

ApplicationConfig headless_config() {
    ApplicationConfig cfg;
    cfg.headless = true;
    cfg.start_in_map_select = true;
    cfg.lan_port = 0;                                                      // the tests do not announce on the real network
    return cfg;
}

bool start_three(Peer& host, Peer& bob, Application& app, const std::string& app_name) {
    if (!host.net.host(0, "Alice", true)) return false;
    host.net.set_map("TINY.LVL");
    if (!bob.net.join("127.0.0.1", host.net.listen_port(), "Bob")) return false;
    for (int i = 0; i < 800 && bob.net.phase() != net::NetGame::Phase::Room; ++i) {          // Bob is seated first (seat 1), the application second
        host.now += 10;
        host.update();
        bob.now += 10;
        bob.update();
        std::this_thread::sleep_for(std::chrono::microseconds(300));
    }
    if (bob.net.phase() != net::NetGame::Phase::Room || bob.net.my_seat() != 1) return false;
    ApplicationConfig cfg = headless_config();
    cfg.net_role = ApplicationConfig::NetRole::Join;
    cfg.net_address = "127.0.0.1";
    cfg.net_port = host.net.listen_port();
    cfg.player_name = app_name;
    if (!app.init(cfg)) return false;
    Trio trio{app, host, bob};
    if (!trio.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && app.net()->my_seat() == 2 && host.net.can_start(); }, 8000)) return false;
    host.net.set_map("SMALL.LVL");
    trio.step(200);
    uint64_t hash = 0;
    if (!net::hash_file(maps_dir() + "SMALL.LVL", hash) || !host.net.start_match(99, hash)) return false;
    return trio.until([&]() {
        return app.state() == AppState::Playing && host.net.phase() == net::NetGame::Phase::Playing && bob.net.phase() == net::NetGame::Phase::Playing;
    }, 8000);
}

char** argv_of(std::vector<std::string>& args, std::vector<char*>& storage) {
    storage.clear();
    for (auto& a : args) storage.push_back(a.data());
    storage.push_back(nullptr);
    return storage.data();
}

bool log_has(const std::deque<std::string>& log, const std::string& needle) {
    for (const auto& line : log) {
        if (line.find(needle) != std::string::npos) return true;
    }
    return false;
}

// The chat box wraps a long text into several lines: the lines joined by one blank are the text again
std::string log_joined(const std::deque<std::string>& log) {
    std::string out;
    for (const auto& line : log) {
        if (!out.empty()) out += ' ';
        out += line;
    }
    return out;
}

Command order(uint8_t seat, uint32_t ant, int16_t x, int16_t y) {
    Command c;
    c.type = CommandType::GroupMove;
    c.issuer = seat;
    c.tile_x = x;
    c.tile_y = y;
    c.ants = {ant};
    return c;
}

// An open ground tile near a team's hill: a goal that the path finder accepts
bool open_goal_near_hill(const sim::SimulationEngine& sim, uint8_t team, int16_t& gx, int16_t& gy) {
    const auto* hill = sim.grid().find_anthill(team);
    if (hill == nullptr) return false;
    for (int32_t d = 4; d <= 14; ++d) {
        for (int32_t dy = -d; dy <= d; ++dy) {
            for (int32_t dx = -d; dx <= d; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != d) continue;
                const sim::TileCoord t{static_cast<int32_t>(hill->x) + 2 + dx, static_cast<int32_t>(hill->y) + 2 + dy};
                if (!sim.grid().in_bounds(t) || sim.grid().is_solid_obstacle(t.x, t.y) || sim.grid().is_solid_object(t) ||
                    sim.grid().terrain_class_at(t) == sim::movement::kTerrainWater) {
                    continue;
                }
                gx = static_cast<int16_t>(t.x);
                gy = static_cast<int16_t>(t.y);
                return true;
            }
        }
    }
    return false;
}

std::vector<uint32_t> ants_of(const sim::SimulationEngine& s, uint8_t player) {
    std::vector<uint32_t> out;
    for (const auto& a : s.get_world_state().ants) {
        if (a.player_id == player) out.push_back(a.id);
    }
    return out;
}

void run_command_line_tests() {
    TEST_CASE("N5.1 Command Line: --name, -N<team><name> (the original's), --team-name, -pnum: (the original's spelling) and -pnum=, --host [port], --join host[:port], --port, --loopback") {
        std::vector<std::string> args;
        std::vector<char*> st;
        args = {"ants", "--name", "Alice Smith", "-N1Bob", "-N3Dave", "-pnum=2", "--team-name", "0", "Zed", "--headless"};
        ApplicationConfig c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_EQ(c.player_name, "Alice Smith");
        ASSERT_EQ(c.team_names[1], "Bob");
        ASSERT_EQ(c.team_names[3], "Dave");
        ASSERT_EQ(c.team_names[0], "Zed");
        ASSERT_EQ(c.team_names[2], "");
        ASSERT_EQ(c.local_player_id, 2);
        ASSERT_TRUE(c.headless);
        ASSERT_TRUE(c.net_role == ApplicationConfig::NetRole::None);
        // the original spells it with a colon (string "pnum:" at 0x1047134)
        args = {"ants", "-pnum:3"};
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_EQ(c.local_player_id, 3);
        args = {"ants", "-pnum:1", "-pnum=2"};
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_EQ(c.local_player_id, 2);                                                 // the last one wins, whichever spelling
        // --room CODE and --token T (protocol 6: a server's room and the credential that came with it)
        args = {"ants", "--join", "play.example.org:4001", "--room", "ROOM-42", "--token", "abc.DEF-123", "--name", "Ann"};
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_EQ(c.net_room, "ROOM-42");
        ASSERT_EQ(c.net_token, "abc.DEF-123");
        // --join-url URL (a server's WebSocket door: how the browser build joins; a native game only parses it)
        args = {"ants", "--join-url", "wss://play.example.org/ws", "--room", "ROOM-42", "--seat", "2", "--name", "Web"};
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_TRUE(c.net_role == ApplicationConfig::NetRole::Join);
        ASSERT_EQ(c.net_url, "wss://play.example.org/ws");
        ASSERT_EQ(c.net_room, "ROOM-42");
        ASSERT_EQ(c.net_seat, 2);
        args = {"ants", "--join", "host"};
        ASSERT_EQ(Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st)).net_url, "");
        ASSERT_EQ(Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st)).net_address, "host");
        {                                                                                  // a native NetGame cannot join through a WebSocket (it joins with TCP)
            ants::sim::SimulationEngine sim;
            ants::net::NetGame game(sim);
            ASSERT_FALSE(game.join_url("wss://play.example.org/ws", "Web"));
            ASSERT_FALSE(game.active());
        }
        args = {"ants", "--room", "no spaces allowed"};                                  // not a room code: ignored
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_EQ(c.net_room, "");
        args = {"ants", "--room", "a/../b"};
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_EQ(c.net_room, "");
        const uint8_t unset = Application::parse_arguments(1, argv_of(args = {"ants"}, st)).local_player_id;
        for (const char* bad : {"-pnum:4", "-pnum:7", "-pnum:-1", "-pnum:"}) {
            args = {"ants", bad};
            c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
            ASSERT_EQ(c.local_player_id, unset);                                         // no team 4 or 7, no digits: ignored
        }
        args = {"ants", "-N", "-N9x", "-N4y", "-N0"};                                   // nothing valid: no team 9, no team 4, an empty name
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_EQ(c.team_names[0], "");
        for (const auto& n : c.team_names) ASSERT_EQ(n, "");
        args = {"ants", "--host"};
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_TRUE(c.net_role == ApplicationConfig::NetRole::Host && c.net_port == 4001);       // the original's port
        args = {"ants", "--host", "5555", "--name", "Queen", "--loopback"};
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_TRUE(c.net_role == ApplicationConfig::NetRole::Host && c.net_port == 5555 && c.net_loopback_only && c.player_name == "Queen");
        args = {"ants", "--host", "--name", "Queen"};                                    // no port: the next word is not a number
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_TRUE(c.net_port == 4001 && c.player_name == "Queen");
        args = {"ants", "--join", "10.0.0.5:4444", "--name", "Bob"};
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_TRUE(c.net_role == ApplicationConfig::NetRole::Join && c.net_address == "10.0.0.5" && c.net_port == 4444 && c.player_name == "Bob");
        args = {"ants", "--join", "example.org"};
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_TRUE(c.net_address == "example.org" && c.net_port == 4001);
        args = {"ants", "--join", "fe80::1"};                                            // several colons: an IPv6 address, no port
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_TRUE(c.net_address == "fe80::1" && c.net_port == 4001);
        args = {"ants", "--join", "host", "--port", "7000"};
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_TRUE(c.net_address == "host" && c.net_port == 7000);
        args = {"ants", "--host"};                                                       // the room announces itself on the game's discovery port ...
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_EQ(c.lan_port, net::kLanDiscoveryPort);
        args = {"ants", "--host", "--lan-port", "4555"};                                 // ... or on another one ...
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_EQ(c.lan_port, 4555);
        args = {"ants", "--host", "--no-lan"};                                           // ... or on none
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_EQ(c.lan_port, 0);
    } TEST_END();

    TEST_CASE("N5.1b Command Line: --seat, --title, --window-pos, --window-size, --grid, --cell, --display, --audio-focus (and what is refused)") {
        std::vector<std::string> args;
        std::vector<char*> st;
        ApplicationConfig c = Application::parse_arguments(0, nullptr);
        ASSERT_EQ(c.net_seat, 255);                                                      // defaults: any seat, no layout, no grid, the display of the window, sound always
        ASSERT_FALSE(c.has_window_pos || c.has_window_size || c.audio_follows_focus);
        ASSERT_EQ(c.grid_cols, 0);
        ASSERT_EQ(c.display_index, -1);
        args = {"ants", "--join", "host", "--seat", "2", "--title", "Ants - Blue"};
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_EQ(c.net_seat, 2);
        ASSERT_EQ(c.title, "Ants - Blue");
        args = {"ants", "--seat", "4"};                                                  // no fifth colour: ignored
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_EQ(c.net_seat, 255);
        args = {"ants", "--seat", "-1"};
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_EQ(c.net_seat, 255);
        args = {"ants", "--window-pos", "100,-20", "--window-size", "800,600"};
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_TRUE(c.has_window_pos && c.window_x == 100 && c.window_y == -20);          // off the left / top of the main display is legal (other displays)
        ASSERT_TRUE(c.has_window_size && c.window_w == 800 && c.window_h == 600);
        args = {"ants", "--window-pos", "nonsense", "--window-size", "10x10"};           // garbage and a window smaller than the game's minimum: not taken
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_FALSE(c.has_window_pos);
        ASSERT_FALSE(c.has_window_size);
        args = {"ants", "--grid", "2x2", "--cell", "3", "--display", "1", "--audio-focus"};
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_TRUE(c.grid_cols == 2 && c.grid_rows == 2 && c.grid_cell == 3 && c.display_index == 1 && c.audio_follows_focus);
        args = {"ants", "--grid", "0x2", "--cell", "-5", "--display", "-7"};
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_TRUE(c.grid_cols == 0 && c.grid_rows == 0);                                // a grid needs at least one cell each way
        ASSERT_EQ(c.grid_cell, 0);
        ASSERT_EQ(c.display_index, -1);
    } TEST_END();

    TEST_CASE("N5.0b --lan-list: what the local network offers is listed (host, address, map, players, version), a silent network says so") {
        uint16_t port = 0;
        {
            auto probe = net::LanBrowser::open(0);                                       // a free UDP port for this test (the game's own port 4001 stays untouched)
            ASSERT_TRUE(probe != nullptr);
            port = probe->port();
        }
        std::ostringstream none;
        ASSERT_EQ(list_lan_rooms(port, 300, none), 0);
        ASSERT_TRUE(none.str().find("No games found.") != std::string::npos);
        Peer host;
        host.net.set_discovery(port, true);
        host.net.set_game_version("v9.9.9");
        ASSERT_TRUE(host.net.host(0, "Queen Anne", true));
        host.net.set_map("TINY.LVL");
        std::ostringstream out;
        const int found = list_lan_rooms(port, 2500, out, [&](uint32_t now) {
            host.now = 1000 + now;
            host.update();
        });
        ASSERT_EQ(found, 1);
        const std::string text = out.str();
        ASSERT_TRUE(text.find("\"Queen Anne\"") != std::string::npos);
        ASSERT_TRUE(text.find("TINY.LVL") != std::string::npos);
        ASSERT_TRUE(text.find("1/4 players") != std::string::npos);
        ASSERT_TRUE(text.find("v9.9.9") != std::string::npos);
        ASSERT_TRUE(text.find("127.0.0.1:" + std::to_string(host.net.listen_port())) != std::string::npos);      // the address to connect to and the room's TCP port
        ASSERT_TRUE(text.find("1 game found.") != std::string::npos);
    } TEST_END();

    TEST_CASE("N5.7 Local Game: The Score Labels Of Teams Without A Name Show Only When Asked For (the browser build has no other players, so no placeholders)") {
        ApplicationConfig cfg = headless_config();
        cfg.player_name = "Alice";
        cfg.team_names[2] = "Carol";
        cfg.label_unnamed_teams = false;                                                  // what the browser build sets
        Application app;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_TRUE(app.start_game("Original-Ants/Maps/SMALL.LVL"));
        ASSERT_EQ(app.hud().roster_mask(), 0x05);                                         // the own team (0) and the named team 2: no "Red:", no "Black:"
        ASSERT_EQ(app.sim().roster_mask(), 0x0F);                                         // the four teams still exist in the game itself
        ASSERT_TRUE(ApplicationConfig{}.label_unnamed_teams);                             // the native default keeps the four labels
    } TEST_END();

    TEST_CASE("N5.8 Local Game: By Default Every Team Has A Score Label (colour words for the unnamed ones)") {
        ApplicationConfig cfg = headless_config();
        Application app;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_TRUE(app.start_game("Original-Ants/Maps/SMALL.LVL"));
        ASSERT_EQ(app.hud().roster_mask(), 0x0F);
    } TEST_END();

    TEST_CASE("N5.2 Names Reach Every Place That Shows One (local game): the simulation's texts, the HUD's labels, the results rows, the own label") {
        ApplicationConfig cfg = headless_config();
        cfg.player_name = "Alice";
        cfg.team_names[1] = "Bob";
        cfg.team_names[2] = "Carol";
        cfg.local_player_id = 0;
        Application app;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_FALSE(app.network_active());
        ASSERT_TRUE(app.start_game("Original-Ants/Maps/SMALL.LVL"));
        ASSERT_EQ(app.hud().get_player_name(), "Alice");
        ASSERT_EQ(app.sim().get_player_name(0), "Alice");                                // the own name belongs to the own team
        ASSERT_EQ(app.sim().get_player_name(1), "Bob");
        ASSERT_EQ(app.sim().get_player_name(2), "Carol");
        ASSERT_EQ(app.sim().get_player_name(3), sim::strings::colour_name(0));            // no name given: the colour word (black)
        ASSERT_EQ(app.hud().team_names()[0], "Alice");
        ASSERT_EQ(app.hud().team_names()[1], "Bob");
        ASSERT_EQ(app.hud().team_names()[3], "");
        // the texts of the simulation carry the names: an alliance, a drop-out
        app.sim().apply_command([&]() { Command c; c.type = CommandType::Drop; c.issuer = 1; return c; }());
        bool named = false;
        for (const auto& n : app.sim().poll_news_events()) named = named || n.message_text.find("Bob dropped out of the game!") != std::string::npos;
        ASSERT_TRUE(named);
        // a network game never sends the user and machine name by default
        ApplicationConfig none = headless_config();
        ASSERT_TRUE(none.player_name.empty());
    } TEST_END();
}

// AI6: computer players in the application (docs/BOTS.md): the command line, the roster and the names of a local game, the refusals, a room whose host runs a bot
void click_fog(Application& app, bool on) {
    const int32_t x = (on ? MapSelectScreen::BTN_FOW_ON_X : MapSelectScreen::BTN_FOW_OFF_X) + 2;
    const int32_t y = (on ? MapSelectScreen::BTN_FOW_ON_Y : MapSelectScreen::BTN_FOW_OFF_Y) + 2;
    app.map_select().handle_mouse_motion(x, y);                                         // a button acts at the release
    app.map_select().handle_mouse_down(x, y, 1);
    app.map_select().handle_mouse_up(x, y, 1);
}

// A bot of the test's own that acts in a way that it chooses (the registry has the idle bot and the worker bot): at every look it sends its first ant to one of two tiles, alternately
class MarchingBot final : public ai::Bot {
public:
    const char* kind() const noexcept override { return "marching"; }
    void start(const ai::BotContext& c) override { seat = c.seat; }
    void think(const ai::BotView& v, ai::Orders& o) override {
        ++looks;
        if (v.mine().empty()) return;
        o.move({v.mine()[0].id}, sim::TileCoord{looks % 2 == 0 ? 8 : 14, looks % 2 == 0 ? 8 : 14});          // (inside every shipped map)
    }
    void on_command(const sim::Command&, Fate fate, uint64_t) override { fates[static_cast<size_t>(fate)]++; }
    uint8_t seat{255};
    int looks{0};
    int fates[4]{};
};

ai::BotSpec bot_spec(const char* text) {
    ai::BotSpec spec;
    std::string why;
    if (!ai::parse_bot_spec(text, spec, why)) std::cout << "    (bad spec " << text << ": " << why << ")\n";
    return spec;
}

void run_bot_tests() {
    TEST_CASE("AI6.1 Command Line: --bot SEAT[:SPEC] Is Repeatable And Keeps Its Order; A Spec That Does Not Parse Is A Startup Error; Nothing Without --bot") {
        std::vector<std::string> args;
        std::vector<char*> st;
        args = {"ants", "--bot", "1", "--bot", "2:hard", "--bot", "3:idle:easy", "--headless"};
        ApplicationConfig c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_TRUE(c.startup_error.empty() && c.headless);
        ASSERT_EQ(c.bots.size(), 3u);
        ASSERT_TRUE(c.bots[0].seat == 1 && c.bots[0].kind == "standard" && c.bots[0].level == ai::Level::Medium);
        ASSERT_TRUE(c.bots[1].seat == 2 && c.bots[1].kind == "standard" && c.bots[1].level == ai::Level::Hard);
        ASSERT_TRUE(c.bots[2].seat == 3 && c.bots[2].kind == "idle" && c.bots[2].level == ai::Level::Easy);
        args = {"ants", "--bot", "2:worker", "-pnum=1"};
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_TRUE(c.bots.size() == 1 && c.bots[0].kind == "worker" && c.local_player_id == 1 && c.startup_error.empty());
        // what does not parse: a startup error with the reason (the first one), the good specs around it are kept
        args = {"ants", "--bot", "1", "--bot", "7", "--bot", "fast"};
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_TRUE(c.bots.size() == 1 && c.startup_error.find("--bot 7") != std::string::npos && c.startup_error.find("0, 1, 2 or 3") != std::string::npos);
        args = {"ants", "--bot", "2:genius"};
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_TRUE(c.bots.empty() && c.startup_error.find("genius") != std::string::npos);
        args = {"ants", "--bot"};
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_TRUE(c.bots.empty() && !c.startup_error.empty());
        // and the program refuses to start with it (nothing is opened)
        {
            Application app;
            c.headless = true;
            ASSERT_FALSE(app.init(c));
            ASSERT_TRUE(app.bots() == nullptr);
        }
        // no --bot: no bots, no error
        args = {"ants", "--headless", "-N1Bob"};
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_TRUE(c.bots.empty() && c.startup_error.empty());
        ASSERT_TRUE(ApplicationConfig{}.bots.empty() && ApplicationConfig{}.startup_error.empty());
    } TEST_END();

    TEST_CASE("AI6.13 Command Line: --teams ffa | A+B (Two Different Seats Of 0 To 3, ffa In Any Case Is The Default, The Last One Wins) Is The Choice Of The START That This Machine Leads (A Game On This Computer Makes It At Once, A Room (--host, --join, --join-url) Sends It With Its START Since Protocol 13): Anything Else Is A Startup Error That Names The Option, ffa Is Fine Everywhere, And It Is No Mode (The Start Menu Still Shows)") {
        std::vector<std::string> args;
        std::vector<char*> st;
        const auto parsed = [&](std::vector<std::string> a) {
            args = std::move(a);
            return Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        };
        {   // valid
            ApplicationConfig c = parsed({"ants", "--teams", "0+1", "--headless"});
            ASSERT_TRUE(c.teams.set && c.teams.a == 0 && c.teams.b == 1 && c.startup_error.empty());
            c = parsed({"ants", "--headless", "--teams", "3+2"});
            ASSERT_TRUE(c.teams.set && c.teams.a == 3 && c.teams.b == 2 && c.startup_error.empty());        // (the order is kept: the first seat invites)
            for (const char* ffa : {"ffa", "FFA", "Ffa"}) {
                c = parsed({"ants", "--teams", ffa, "--headless"});
                ASSERT_TRUE(!c.teams.set && c.startup_error.empty());
            }
            c = parsed({"ants", "--teams", "2+3", "--teams", "ffa", "--headless"});                           // the last one wins
            ASSERT_TRUE(!c.teams.set && c.startup_error.empty());
            c = parsed({"ants", "--teams", "ffa", "--teams", "1+0", "--headless"});
            ASSERT_TRUE(c.teams.set && c.teams.a == 1 && c.teams.b == 0 && c.startup_error.empty());
            c = parsed({"ants", "--headless"});                                                              // not asked for: free for all
            ASSERT_TRUE(!c.teams.set && c.startup_error.empty());
            c = parsed({"ants", "--bot", "1", "--bot", "2", "--bot", "3", "--teams", "0+1"});                 // with the bots: both are kept
            ASSERT_TRUE(c.bots.size() == 3 && c.teams.set && c.teams.a == 0 && c.teams.b == 1 && c.startup_error.empty());
        }
        {   // anything else is a startup error that names the option (and the game does not start)
            for (const char* bad : {"0+0", "1+1", "0+4", "4+0", "01", "0+", "+1", "0 + 1", "0+1+2", "fff", "none", "-1+0", ""}) {
                ApplicationConfig c = parsed({"ants", "--headless", "--teams", bad});
                ASSERT_TRUE(!c.teams.set);
                ASSERT_TRUE(c.startup_error.find(std::string("--teams ") + bad + ":") == 0);
                Application app;
                ASSERT_FALSE(app.init(c));
            }
            ApplicationConfig same = parsed({"ants", "--headless", "--teams", "2+2"});
            ASSERT_TRUE(same.startup_error.find("two different seats") != std::string::npos);
            ApplicationConfig missing = parsed({"ants", "--headless", "--teams"});
            ASSERT_TRUE(missing.startup_error.find("--teams needs ffa or two seats like 0+1") == 0);
            ApplicationConfig first = parsed({"ants", "--headless", "--teams", "9", "--teams", "0+1"});        // the first problem is the one that is told
            ASSERT_TRUE(first.startup_error.find("--teams 9:") == 0);
            ApplicationConfig two = parsed({"ants", "--headless", "--teams", "9", "--teams", "8"});           // (and a second bad value does not replace it)
            ASSERT_TRUE(two.startup_error.find("--teams 9:") == 0);
        }
        {   // a room takes --teams too (protocol 13: it is the choice of the START that this machine leads; changed on purpose: protocol 12 refused it with "--teams is for a game on this computer"),
            // whatever the order of the options; ffa is the default of a room too
            for (const std::vector<std::string>& line : {std::vector<std::string>{"ants", "--teams", "0+1", "--host"}, std::vector<std::string>{"ants", "--host", "--teams", "0+1"},
                                                          std::vector<std::string>{"ants", "--join", "127.0.0.1", "--teams", "0+1"}, std::vector<std::string>{"ants", "--teams", "0+1", "--join-url", "ws://example.test/ws"}}) {
                const ApplicationConfig c = parsed(line);
                ASSERT_TRUE(c.startup_error.empty() && c.teams.set && c.teams.a == 0 && c.teams.b == 1 && c.net_role != ApplicationConfig::NetRole::None);
            }
            ApplicationConfig ok = parsed({"ants", "--teams", "ffa", "--host"});
            ASSERT_TRUE(ok.startup_error.empty() && !ok.teams.set);
            for (const char* bad : {"0+0", "0+4", "none", ""}) {                                              // a bad value is a startup error in a room too
                const ApplicationConfig c = parsed({"ants", "--host", "--teams", bad});
                ASSERT_TRUE(!c.teams.set && c.startup_error.find(std::string("--teams ") + bad + ":") == 0);
            }
            const ApplicationConfig both = parsed({"ants", "--join", "127.0.0.1", "--room", "R-1", "--fill-bots", "none,none,easy,hard", "--teams", "0+2"});      // (the two choices of one START)
            ASSERT_TRUE(both.startup_error.empty() && both.teams.set && both.teams.a == 0 && both.teams.b == 2 && both.fill_bots == net::FillPlan(std::array<net::FillLevel, 4>{net::FillLevel::None, net::FillLevel::None, net::FillLevel::Easy, net::FillLevel::Hard}));
        }
        {   // it is no mode: a native game that is started with --teams alone still shows the start menu, which takes the choice as the panel's own
            ApplicationConfig c = parsed({"ants", "--teams", "0+1"});
            ASSERT_TRUE(c.start_menu && c.teams.set);
            c = parsed({"ants", "--teams", "0+1", "--map", "Original-Ants/Maps/TINY.LVL"});
            ASSERT_FALSE(c.start_menu);
        }
    } TEST_END();

    TEST_CASE("AI6.2 Refusals At Startup: A Seat Clash With The Local Player (Whatever The Order Of The Options), Two Bots On One Seat, --join With --bot, A Room's Host Seat") {
        const auto refused = [](const std::function<void(ApplicationConfig&)>& setup) {
            ApplicationConfig cfg = headless_config();
            setup(cfg);
            Application app;
            const bool ok = app.init(cfg);
            return !ok && app.bots() == nullptr;
        };
        ASSERT_TRUE(refused([](ApplicationConfig& c) { c.bots = {bot_spec("0")}; }));                                      // the local player sits at seat 0 by default
        ASSERT_TRUE(refused([](ApplicationConfig& c) { c.local_player_id = 1; c.bots = {bot_spec("1")}; }));
        ASSERT_TRUE(refused([](ApplicationConfig& c) { c.bots = {bot_spec("2"), bot_spec("2:hard")}; }));
        ASSERT_TRUE(refused([](ApplicationConfig& c) {
            c.net_role = ApplicationConfig::NetRole::Join;
            c.net_address = "127.0.0.1";
            c.bots = {bot_spec("1")};
        }));
        ASSERT_TRUE(refused([](ApplicationConfig& c) {
            c.net_role = ApplicationConfig::NetRole::Host;
            c.net_port = 0;
            c.net_loopback_only = true;
            c.bots = {bot_spec("0")};                                                                                  // the host sits at seat 0
        }));
        // via the command line the order of the options does not matter: --bot first, the seat afterwards
        {
            std::vector<std::string> args = {"ants", "--bot", "1", "-pnum=1", "--headless", "--no-lan"};
            std::vector<char*> st;
            ApplicationConfig c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
            Application app;
            ASSERT_FALSE(app.init(c));
        }
        // and a good one starts
        {
            ApplicationConfig cfg = headless_config();
            cfg.bots = {bot_spec("1"), bot_spec("2:hard")};
            Application app;
            ASSERT_TRUE(app.init(cfg));
            ASSERT_TRUE(app.bots() == nullptr);                                                                        // the setup screen: no game yet, no controller yet
        }
    } TEST_END();

    TEST_CASE("AI6.3 Local Game With Bots: The Roster Is The Local Player And The Bots (No Hill, No Ants For The Empty Seat), The Names Are The Bots' (An Explicit Name Wins), The Controller Runs On The Ticks And Stops With The Match") {
        ApplicationConfig cfg = headless_config();
        cfg.player_name = "Alice";
        cfg.bots = {bot_spec("1:medium"), bot_spec("3:idle:hard")};
        cfg.team_names[3] = "Zed";
        Application app;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_TRUE(app.bots() == nullptr);
        ASSERT_TRUE(app.start_game("Original-Ants/Maps/SMALL.LVL"));
        ASSERT_EQ(app.sim().roster_mask(), 0x0B);                                                                      // seats 0, 1 and 3
        ASSERT_EQ(app.sim().grid().anthills().size(), 3u);
        ASSERT_EQ(app.hud().roster_mask(), 0x0B);
        ASSERT_EQ(app.sim().get_player_name(0), "Alice");
        ASSERT_EQ(app.sim().get_player_name(1), "Bot (Medium)");
        ASSERT_EQ(app.sim().get_player_name(2), sim::strings::colour_name(1));                                          // nobody sits there: the colour word of seat 2 (red)
        ASSERT_EQ(app.sim().get_player_name(3), "Zed");                                                                 // -N / --team-name wins over "Bot (Idle)"
        ASSERT_EQ(app.hud().team_names()[1], "Bot (Medium)");
        ASSERT_EQ(app.hud().team_names()[3], "Zed");
        ASSERT_TRUE(app.bots() != nullptr);
        ASSERT_EQ(app.bots()->seat_mask(), 0x0A);
        ASSERT_TRUE(app.bots()->stats(1).decisions == 0 && app.bots()->stats(3).decisions == 0);
        ASSERT_TRUE(ants_of(app.sim(), 2).empty() && !ants_of(app.sim(), 1).empty());
        const uint64_t hash0 = app.sim().state_hash().total;
        // the ticks of the local loop reach the controller: the bots look at the world (the idle one never acts; the standard one sends its ants to the food).
        // The first kDialogSteps steps are the "Get ready" dialog, in which the simulation waits, so 400 ticks take kDialogSteps + 400 steps
        for (int i = 0; i < kDialogSteps + 400; ++i) app.update_simulation(0.05f);
        ASSERT_EQ(app.sim().current_tick(), 400u);
        // medium: every 20 ticks, hard: every 4, the first look on tick 1 + seat (the simulation did not run behind the dialog, so tick 1 is the first there is): seat 1 on 2, 22, ... 382 (20 looks),
        // seat 3 on 4, 8, ... 400 (100 looks)
        ASSERT_TRUE(app.bots()->stats(1).decisions == 20 && app.bots()->stats(3).decisions == 100);
        ASSERT_TRUE(app.bots()->stats(1).released >= 1 && app.bots()->stats(3).released == 0 && app.bots()->stats(1).filtered == 0);
        ASSERT_TRUE(app.sim().state_hash().total != hash0);                                                              // the world moved on (food, queues, clock)
        // the match ends: the results name the bots, and the controller falls silent
        app.sim().set_match_time_remaining_ms(1000);
        for (int i = 0; i < 200 && !app.sim().is_match_over(); ++i) app.update_simulation(0.05f);
        ASSERT_TRUE(app.sim().is_match_over());
        app.update_results(0.0f);
        app.update_results(0.3f);
        ASSERT_FALSE(app.scorecard().rows().empty());
        std::vector<std::string> row_names;
        for (const auto& row : app.scorecard().rows()) row_names.push_back(row.name);
        ASSERT_TRUE(std::find(row_names.begin(), row_names.end(), "Bot (Medium)") != row_names.end());
        ASSERT_TRUE(std::find(row_names.begin(), row_names.end(), "Alice") != row_names.end());
        ASSERT_TRUE(std::find(row_names.begin(), row_names.end(), "Zed") != row_names.end());
        ASSERT_EQ(row_names.size(), 3u);                                                                                 // the empty seat has no row
        const uint32_t looks = app.bots()->stats(1).decisions;
        for (int i = 0; i < 100; ++i) app.update_simulation(0.05f);
        ASSERT_EQ(app.bots()->stats(1).decisions, looks);
        // back to the setup screen: no bots; a new game builds new ones
        app.return_to_map_select();
        ASSERT_TRUE(app.bots() == nullptr);
        ASSERT_TRUE(app.start_game("Original-Ants/Maps/TINY.LVL"));
        ASSERT_TRUE(app.bots() != nullptr && app.bots()->stats(1).decisions == 0);
        ASSERT_EQ(app.sim().roster_mask(), 0x0B);
    } TEST_END();

    TEST_CASE("AI6.4 Local Game With --map And --bot: The Game Is Running At Once With The Roster Of The Taken Seats; The Local Player May Sit Anywhere") {
        ApplicationConfig cfg = headless_config();
        cfg.start_in_map_select = false;
        cfg.default_map_path = "Original-Ants/Maps/TINY.LVL";
        cfg.local_player_id = 1;
        cfg.bots = {bot_spec("2:hard")};
        Application app;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_EQ(app.state(), AppState::Playing);
        ASSERT_EQ(app.local_player_id(), 1);
        ASSERT_EQ(app.sim().roster_mask(), 0x06);
        ASSERT_EQ(app.sim().grid().anthills().size(), 2u);
        ASSERT_EQ(app.sim().get_player_name(2), "Bot (Hard)");
        ASSERT_TRUE(app.bots() != nullptr && app.bots()->seat_mask() == 0x04);
        ASSERT_FALSE(app.hud().is_match_start_modal_active());                                                         // (a game that starts straight into its match has no "Get ready" dialog to wait for)
        for (int i = 0; i < 100; ++i) app.update_simulation(0.05f);
        ASSERT_EQ(app.sim().current_tick(), 100u);                                                                      // the match runs at once
        ASSERT_TRUE(app.bots()->stats(2).decisions >= 24);                                                              // (the bot looks from tick 3 on, every 4 ticks)
        // the same game without a bot has no controller and all four teams, as ever
        ApplicationConfig plain = headless_config();
        plain.start_in_map_select = false;
        plain.default_map_path = "Original-Ants/Maps/TINY.LVL";
        Application normal;
        ASSERT_TRUE(normal.init(plain));
        ASSERT_TRUE(normal.bots() == nullptr);
        ASSERT_EQ(normal.sim().roster_mask(), 0x0F);
    } TEST_END();

    TEST_CASE("AI6.5 Fog Of War And Bots: START Is Refused With The Reason On The Setup Screen And On stderr; Without Fog It Starts; A Game Without --bot Plays With Fog As Ever And Has No Controller") {
        ApplicationConfig cfg = headless_config();
        cfg.bots = {bot_spec("1")};
        Application app;
        ASSERT_TRUE(app.init(cfg));
        click_fog(app, true);
        ASSERT_TRUE(app.map_select().is_fog_of_war_enabled());
        ASSERT_FALSE(app.start_game("Original-Ants/Maps/SMALL.LVL"));
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_TRUE(app.bots() == nullptr);
        ASSERT_TRUE(app.map_select().room().status.find("Fog of War") != std::string::npos);                              // the prompt line of the setup screen says why
        click_fog(app, false);
        ASSERT_FALSE(app.map_select().is_fog_of_war_enabled());
        ASSERT_TRUE(app.start_game("Original-Ants/Maps/SMALL.LVL"));
        ASSERT_EQ(app.state(), AppState::Playing);
        ASSERT_TRUE(app.map_select().room().status.empty());                                                               // the refusal is gone
        ASSERT_TRUE(app.bots() != nullptr && !app.sim().is_fog_of_war_enabled());
        // no --bot: fog works, nothing of the bots exists
        ApplicationConfig none = headless_config();
        Application normal;
        ASSERT_TRUE(normal.init(none));
        click_fog(normal, true);
        ASSERT_TRUE(normal.start_game("Original-Ants/Maps/SMALL.LVL"));
        ASSERT_TRUE(normal.sim().is_fog_of_war_enabled());
        ASSERT_TRUE(normal.bots() == nullptr);
        ASSERT_EQ(normal.sim().roster_mask(), 0x0F);
        for (int i = 0; i < 50; ++i) normal.update_simulation(0.05f);
        ASSERT_TRUE(normal.bots() == nullptr);
    } TEST_END();

    TEST_CASE("AI6.11 --play Does Nothing In A Room: A Host With The Flag And A Map Stays On The Room's Setup Screen (No Match Starts, No Bot Runs); The Flag Is For A Game Of One Machine") {
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Host;
        cfg.net_port = 0;
        cfg.net_loopback_only = true;
        cfg.player_name = "Alice";
        cfg.play_at_once = true;
        cfg.default_map_path = "Original-Ants/Maps/TINY.LVL";
        Application app;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_TRUE(app.network_active() && app.net()->is_host());
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_FALSE(app.match_running());
        app.pump_network(0.01f);
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_FALSE(app.match_running());
        ASSERT_TRUE(app.bots() == nullptr);
    } TEST_END();

    TEST_CASE("AI6.6 Room With A Bot: The Host's Setup Screen Shows The Bot, Fog Is Refused, START Runs The Bot On The Host's Machine, The Guest Stays Bit-Identical Without Any Bot Code") {
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Host;
        cfg.net_port = 0;
        cfg.net_loopback_only = true;
        cfg.player_name = "Alice";
        cfg.bots = {bot_spec("2:medium")};
        Application app;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_TRUE(app.network_active() && app.net()->is_host());
        app.pump_network(0.01f);
        ASSERT_TRUE(app.map_select().room().seats[2].occupied && app.map_select().room().seats[2].name == "Bot (Medium)");
        ASSERT_TRUE(app.map_select().room().seats[2].thumb == MapSelectScreen::Thumb::Good);
        ASSERT_TRUE(app.net()->room().slots[2].state == net::SlotState::Bot);
        ASSERT_TRUE(app.bots() == nullptr);                                                                                // no match yet
        click_fog(app, true);                                                                                              // refused: a bot would see through it, the screen shows Off again
        ASSERT_FALSE(app.net()->room().fog);
        ASSERT_FALSE(app.map_select().is_fog_of_war_enabled());
        Peer bob;
        ASSERT_TRUE(bob.net.join("127.0.0.1", app.net()->listen_port(), "Bob"));
        Duo duo{app, bob};
        ASSERT_TRUE(duo.until([&]() { return app.net()->room().slots[1].state == net::SlotState::Client && app.net()->can_start(); }, 8000));
        ASSERT_EQ(bob.net.my_seat(), 1);                                                                                   // the first free seat: the bot has seat 2
        ASSERT_TRUE(bob.net.room().slots[2].state == net::SlotState::Bot && bob.net.room().slots[2].name == "Bot (Medium)");
        app.map_select().handle_key_down(SDLK_RETURN);
        ASSERT_TRUE(duo.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 8000));
        ASSERT_EQ(app.sim().roster_mask(), 0x07);
        ASSERT_EQ(app.sim().get_player_name(2), "Bot (Medium)");
        ASSERT_EQ(app.hud().team_names()[2], "Bot (Medium)");
        ASSERT_TRUE(app.bots() != nullptr && app.bots()->seat_mask() == 0x04);
        duo.step(15000 + kDialogMs);                                                                                       // (the host seals the first turn kDialogMs after the match began: 15 s of play follow)
        ASSERT_TRUE(app.bots()->stats(2).decisions >= 10);                                                                  // it looks every second of the match
        ASSERT_EQ(app.bots()->stats(2).rejected, 0u);
        ASSERT_EQ(bob.sim.roster_mask(), 0x07);
        app.net()->freeze();                                                                                                // the host stops sealing: what is in flight arrives, then both are at the same tick
        duo.step(3000);
        ASSERT_EQ(app.sim().current_tick(), bob.sim.current_tick());
        ASSERT_TRUE(app.sim().state_hash() == bob.sim.state_hash());                                                         // the guest ran no bot code and plays the same game
        ASSERT_FALSE(app.net()->desynced() || bob.net.desynced());
        app.quit();
        app.return_to_map_select();
        ASSERT_TRUE(app.bots() == nullptr);
    } TEST_END();

    TEST_CASE("AI6.7 A Bot That Acts, In A Local Game: Its Commands Go Through The Application's Door Into The Simulation With The Bot's Seat, None Is Rejected, And Its Ant Really Moves") {
        ApplicationConfig cfg = headless_config();
        cfg.bots = {bot_spec("1:hard")};
        cfg.bot_factory = [](const ai::BotSpec&) { return std::make_unique<MarchingBot>(); };
        Application app;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_TRUE(app.start_game("Original-Ants/Maps/SMALL.LVL"));
        ASSERT_TRUE(app.bots() != nullptr && app.bots()->seat_mask() == 0x02);
        const std::vector<uint32_t> mine = ants_of(app.sim(), 1);
        ASSERT_FALSE(mine.empty());
        sim::TileCoord start{};
        for (const auto& a : app.sim().get_world_state().ants) {
            if (a.id == mine[0]) start = sim::TileCoord{a.tile_x, a.tile_y};
        }
        for (int i = 0; i < kDialogSteps + 200; ++i) app.update_simulation(0.05f);                                        // (200 ticks after the dialog)
        const auto& st = app.bots()->stats(1);
        ASSERT_TRUE(st.released >= 1);
        ASSERT_EQ(st.rejected, 0u);                                                                                      // the engine accepted what the door carried
        ASSERT_EQ(st.filtered, 0u);
        bool moved = false;
        for (const auto& a : app.sim().get_world_state().ants) {
            if (a.id == mine[0]) moved = a.tile_x != start.x || a.tile_y != start.y || a.state == sim::UnitState::Walking;
        }
        ASSERT_TRUE(moved);                                                                                              // the order reached the ant
        ASSERT_TRUE(ants_of(app.sim(), 0).size() > 0 && app.bots()->stats(1).decisions >= 40);
    } TEST_END();

    TEST_CASE("AI6.8 A Bot That Acts, In A Host's Room: Its Commands Go Through The Room's Door Into The Sequencer With The Bot's Seat, Reach The Guest In The Turn Stream, And Both Machines Stay Bit-Identical") {
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Host;
        cfg.net_port = 0;
        cfg.net_loopback_only = true;
        cfg.player_name = "Alice";
        cfg.bots = {bot_spec("2:hard")};
        cfg.bot_factory = [](const ai::BotSpec&) { return std::make_unique<MarchingBot>(); };
        Application app;
        ASSERT_TRUE(app.init(cfg));
        Peer bob;
        ASSERT_TRUE(bob.net.join("127.0.0.1", app.net()->listen_port(), "Bob"));
        Duo duo{app, bob};
        ASSERT_TRUE(duo.until([&]() { return app.net()->room().slots[1].state == net::SlotState::Client && app.net()->can_start(); }, 8000));
        app.map_select().handle_key_down(SDLK_RETURN);
        ASSERT_TRUE(duo.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 8000));
        ASSERT_TRUE(app.bots() != nullptr && app.bots()->seat_mask() == 0x04);
        const std::vector<uint32_t> bots_ants = ants_of(app.sim(), 2);
        ASSERT_FALSE(bots_ants.empty());
        duo.step(20000);
        const auto& st = app.bots()->stats(2);
        ASSERT_TRUE(st.released >= 3);
        ASSERT_EQ(st.rejected, 0u);                                                                                      // (the sink answers Applied when the sequencer took the command)
        app.net()->freeze();
        duo.step(3000);
        ASSERT_EQ(app.sim().current_tick(), bob.sim.current_tick());
        ASSERT_TRUE(app.sim().state_hash() == bob.sim.state_hash());
        ASSERT_FALSE(app.net()->desynced() || bob.net.desynced());
        // the guest saw the bot's orders: its copy of the bot's first ant stands where the host's does, and that is not where it started (the guest ran no bot code)
        sim::TileCoord on_host{};
        sim::TileCoord on_guest{};
        for (const auto& a : app.sim().get_world_state().ants) {
            if (a.id == bots_ants[0]) on_host = sim::TileCoord{a.tile_x, a.tile_y};
        }
        for (const auto& a : bob.sim.get_world_state().ants) {
            if (a.id == bots_ants[0]) on_guest = sim::TileCoord{a.tile_x, a.tile_y};
        }
        ASSERT_TRUE(on_host.x == on_guest.x && on_host.y == on_guest.y);
        app.quit();
        app.return_to_map_select();
    } TEST_END();

    // A left click at the middle of one of the own ants of a running match, the way the window delivers it (the camera is put on the ant first: the start view may not show it)
    const auto click_own_ant = [](Application& app, uint32_t ant) {
        for (const auto& a : app.sim().get_world_state().ants) {
            if (a.id != ant) continue;
            app.renderer().camera().center_on(a.px, a.py, app.sim().grid().width(), app.sim().grid().height());
            int32_t sx = 0;
            int32_t sy = 0;
            if (!app.renderer().camera().world_to_screen(a.px, a.py, sx, sy)) return false;
            SDL_MouseButtonEvent b{};
            b.type = SDL_MOUSEBUTTONDOWN;
            b.button = SDL_BUTTON_LEFT;
            b.state = SDL_PRESSED;
            b.clicks = 1;
            b.x = sx;
            b.y = sy;
            app.handle_mouse_button(b);
            b.type = SDL_MOUSEBUTTONUP;
            b.state = SDL_RELEASED;
            app.handle_mouse_button(b);
            return true;
        }
        return false;
    };

    TEST_CASE("AI6.9 The Opening Of A Local Game (--bot 1:LEVEL, Every Level): The \"Get Ready\" Dialog Is Up For 100 Steps Of 50 Ms Of The Frame Clock And The Simulation Does Not Run Meanwhile (Tick 0 For The Whole Dialog, The Match's Clock Shows Its Full Time), So The Bot Neither Looks Nor Sends Anything; Tick 1 Runs On The Step After The Dialog's Last, The Bot's First Look Is On Tick 1 + Seat And Its First Order 75 To 125 Percent Of Its Reaction Time Later; A Click On An Own Ant Selects Nothing In The Dialog And Selects It After It") {
        for (const char* spec_text : {"1:easy", "1:medium", "1:hard"}) {
            const ai::BotSpec spec = bot_spec(spec_text);
            const ai::Profile profile = ai::profile_for(spec.level);
            ApplicationConfig cfg = headless_config();
            cfg.bots = {spec};
            cfg.bot_factory = [](const ai::BotSpec&) { return std::make_unique<MarchingBot>(); };       // acts at every look
            Application app;
            ASSERT_TRUE(app.init(cfg));
            ASSERT_TRUE(app.start_game("Original-Ants/Maps/SMALL.LVL"));
            ASSERT_TRUE(app.bots() != nullptr && app.bots()->seat_mask() == 0x02);
            ASSERT_TRUE(app.bots()->start_hold() == ai::kStartHoldTicks);                                 // every product path has the opening of the product: one token, a first look on the first tick
            const std::vector<uint32_t> bots_ants = ants_of(app.sim(), 1);
            const std::vector<uint32_t> own = ants_of(app.sim(), 0);
            ASSERT_TRUE(!bots_ants.empty() && !own.empty());
            ASSERT_EQ(app.sim().current_tick(), 0u);
            ASSERT_TRUE(app.hud().is_match_start_modal_active());
            const uint32_t full_time = app.sim().get_match_time_remaining_ms();
            ASSERT_TRUE(full_time > 0);
            const uint64_t still_hash = app.sim().state_hash().total;
            for (int step = 1; step <= kDialogSteps - 1; ++step) {                                       // steps 1 .. 99
                app.update_simulation(0.05f);
                ASSERT_EQ(app.sim().current_tick(), 0u);                                                  // the simulation waits for the dialog ...
                ASSERT_EQ(app.sim().get_match_time_remaining_ms(), full_time);                            // ... the clock shows the match's full time (the HUD's clock text is a function of it) ...
                ASSERT_TRUE(app.hud().is_match_start_modal_active());                                    // ... the dialog is up ...
                ASSERT_TRUE(app.bots()->stats(1).decisions == 0 && app.bots()->stats(1).released == 0);  // ... and the bot has neither looked nor sent anything to the engine (nothing ticked)
            }
            ASSERT_TRUE(app.sim().state_hash().total == still_hash);                                      // the world is a still picture behind the dialog
            ASSERT_TRUE(click_own_ant(app, own[0]));                                                      // step 99: the dialog takes the click
            ASSERT_TRUE(app.hud().get_selected_ant_ids().empty() && app.hud().get_selected_ant_id() == 0u);
            ASSERT_TRUE(app.hud().is_match_start_modal_active());
            app.update_simulation(0.05f);                                                                  // step 100: the dialog closes, the simulation has not run yet
            ASSERT_FALSE(app.hud().is_match_start_modal_active());
            ASSERT_EQ(app.sim().current_tick(), 0u);
            ASSERT_EQ(app.sim().get_match_time_remaining_ms(), full_time);
            ASSERT_TRUE(click_own_ant(app, own[0]));                                                      // the same click, now: the ant is selected (a person can order from the first tick)
            ASSERT_FALSE(app.hud().get_selected_ant_ids().empty());
            ASSERT_TRUE(app.hud().get_selected_ant_id() != 0u);
            app.update_simulation(0.05f);                                                                  // step 101: tick 1 runs, right after the dialog
            ASSERT_EQ(app.sim().current_tick(), 1u);
            ASSERT_TRUE(app.sim().get_match_time_remaining_ms() < full_time);                             // the clock runs from here
            ASSERT_EQ(app.bots()->stats(1).decisions, 0u);                                                 // (the bot's first look is on tick 2: seat 1)
            app.update_simulation(0.05f);                                                                  // tick 2
            ASSERT_EQ(app.bots()->stats(1).decisions, 1u);
            ASSERT_EQ(app.bots()->stats(1).released, 0u);
            uint64_t first = 0;
            for (int t = 3; t <= 400; ++t) {
                app.update_simulation(0.05f);
                if (first == 0 && app.bots()->stats(1).released > 0) first = static_cast<uint64_t>(t);
            }
            ASSERT_TRUE(first != 0);
            ASSERT_TRUE(first >= 2u + profile.reaction_delay * 3 / 4 && first <= 2u + profile.reaction_delay * 5 / 4);       // 75 to 125 percent of the reaction time after the first look
            ASSERT_EQ(app.bots()->stats(1).rejected, 0u);
        }
    } TEST_END();

    TEST_CASE("AI6.10 The Opening Of A Host's Room: The Host Seals The First Turn 5 s After The Match Began (Protocol 12), So The Dialog Is Up Exactly While No Tick Has Run, The Bot Seat Looks And Sends Nothing Before It, The Guest's Engine Has Run No Tick Either, No Turn Carries A Command Of The Bot's Seat Before The Bot's First Look Plus Its Reaction Time, And The Bot Does Play After It") {
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Host;
        cfg.net_port = 0;
        cfg.net_loopback_only = true;
        cfg.player_name = "Alice";
        cfg.bots = {bot_spec("2:hard")};
        cfg.bot_factory = [](const ai::BotSpec&) { return std::make_unique<MarchingBot>(); };
        Application app;
        ASSERT_TRUE(app.init(cfg));
        Peer bob;
        std::vector<std::pair<uint64_t, Command>> turn_stream;                                          // what the guest's engine applied, and its tick count at the time
        bob.net.set_on_command([&](const Command& c, const sim::CommandResult&) { turn_stream.emplace_back(bob.sim.current_tick(), c); });
        ASSERT_TRUE(bob.net.join("127.0.0.1", app.net()->listen_port(), "Bob"));
        Duo duo{app, bob};
        ASSERT_TRUE(duo.until([&]() { return app.net()->room().slots[1].state == net::SlotState::Client && app.net()->can_start(); }, 8000));
        app.map_select().handle_key_down(SDLK_RETURN);
        ASSERT_TRUE(duo.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 8000));
        ASSERT_TRUE(app.bots() != nullptr && app.bots()->seat_mask() == 0x04 && app.bots()->start_hold() == ai::kStartHoldTicks);
        ASSERT_TRUE(app.hud().is_match_start_modal_active());
        ASSERT_EQ(app.sim().current_tick(), 0u);
        // the dialog is up exactly while the host has run no tick: no tick runs for kDialogMs (the first turn is sealed that long after the match began), and the bot has not looked meanwhile
        uint32_t waited_ms = 0;
        while (app.sim().current_tick() == 0 && waited_ms < 20000) {
            ASSERT_TRUE(app.hud().is_match_start_modal_active());
            ASSERT_TRUE(app.bots()->stats(2).decisions == 0 && app.bots()->stats(2).released == 0);
            ASSERT_EQ(bob.sim.current_tick(), 0u);                                                      // the guest's engine waits for the first turn as well
            duo.step(10);
            waited_ms += 10;
        }
        ASSERT_TRUE(app.sim().current_tick() >= 1);
        ASSERT_FALSE(app.hud().is_match_start_modal_active());                                           // the dialog ended with the first turn that ran
        ASSERT_TRUE(waited_ms >= kDialogMs && waited_ms <= kDialogMs + 150);                             // 5 s, and the second turn (the runner starts with two in hand) and a step
        duo.step(15000);
        ASSERT_TRUE(app.bots()->stats(2).released >= 3 && app.bots()->stats(2).rejected == 0);
        app.net()->freeze();
        duo.step(3000);                                                                                    // what is in flight arrives and executes
        std::vector<uint64_t> bot_ticks;
        for (const auto& e : turn_stream) {
            if (e.second.issuer == 2) bot_ticks.push_back(e.first);
        }
        ASSERT_TRUE(bot_ticks.size() >= 3);                                                                // the bot played (its commands are in the guest's turn stream, with its seat)
        const ai::Profile hard = ai::profile_for(ai::Level::Hard);
        ASSERT_TRUE(bot_ticks.front() >= 3u + hard.reaction_delay * 3 / 4);                                // the first look is on tick 3 (seat 2), its first order leaves 6 to 10 ticks later (and travels through a turn)
        ASSERT_EQ(app.sim().current_tick(), bob.sim.current_tick());
        ASSERT_TRUE(app.sim().state_hash() == bob.sim.state_hash());
        ASSERT_FALSE(app.net()->desynced() || bob.net.desynced());
        app.quit();
        app.return_to_map_select();
    } TEST_END();

    TEST_CASE("AI6.12 A Room With A Bot Says Why Its Bot Declined: Bob's Invitation Is Accepted (Three Teams Play), The Host's Own Invitation To The Bot Is Declined And ONE Line Of The Chat Log Says That The Bot Has A Teammate (The Slot Of The Room Says Bot), Bob's Refusal Of The Host's Invitation Is A Person's And Adds No Line; Both Machines Stay Identical") {
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Host;
        cfg.net_port = 0;
        cfg.net_loopback_only = true;
        cfg.player_name = "Alice";
        cfg.bots = {bot_spec("2:hard")};
        Application app;
        ASSERT_TRUE(app.init(cfg));
        Peer bob;
        ASSERT_TRUE(bob.net.join("127.0.0.1", app.net()->listen_port(), "Bob"));
        Duo duo{app, bob};
        ASSERT_TRUE(duo.until([&]() { return app.net()->room().slots[1].state == net::SlotState::Client && app.net()->can_start(); }, 8000));
        app.map_select().handle_key_down(SDLK_RETURN);
        ASSERT_TRUE(duo.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 8000));
        ASSERT_TRUE(app.net()->room().slots[2].state == net::SlotState::Bot);
        ASSERT_TRUE(duo.until([&]() { return app.sim().current_tick() > 0 && bob.sim.current_tick() > 0; }, 8000));
        duo.step(1000);
        const std::string has_teammate = "Bot (Hard) already has a teammate.";
        const auto count_of = [](const std::string& text, const std::string& what) {
            size_t n = 0;
            for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + what.size())) ++n;
            return n;
        };
        const auto transcript = [&]() { return app.hud().chat_transcript(std::string()); };
        // Bob (seat 1) asks the bot (seat 2): three teams play, the bot accepts
        Command invite;
        invite.type = CommandType::AllianceInvite;
        invite.other_player = 2;
        ASSERT_EQ(bob.net.submit(invite).status, sim::CommandResult::Status::Applied);
        ASSERT_TRUE(duo.until([&]() { return app.sim().stats_manager().are_allies(1, 2) && bob.sim.stats_manager().are_allies(1, 2); }, 20000));
        ASSERT_EQ(count_of(transcript(), "teammate."), 0u);                                       // nothing was declined yet
        // the host asks Bob, and Bob (a person) says no: the original's text only
        app.hud().request_team_up(app.sim(), 1);
        ASSERT_TRUE(duo.until([&]() { return app.hud().alliance_dialog() == HUD::AllianceDialog::Waiting; }, 5000));
        Command deny;
        deny.type = CommandType::AllianceDeny;
        deny.other_player = 0;
        ASSERT_EQ(bob.net.submit(deny).status, sim::CommandResult::Status::Applied);
        ASSERT_TRUE(duo.until([&]() { return app.hud().alliance_dialog() == HUD::AllianceDialog::None && app.hud().status_line().text() == "Bob rejected teaming up"; }, 5000));
        duo.step(600);
        ASSERT_EQ(count_of(transcript(), "teammate."), 0u);
        ASSERT_EQ(count_of(transcript(), "three or more teams"), 0u);
        // the host asks the bot, which has Bob as its teammate: it declines, and the line names it as the room does
        app.hud().request_team_up(app.sim(), 2);
        bool said_no = false;
        ASSERT_TRUE(duo.until([&]() {
            said_no = said_no || app.hud().status_line().text() == "Bot (Hard) rejected teaming up";
            return count_of(transcript(), has_teammate) > 0;
        }, 20000));
        ASSERT_TRUE(said_no);                                                                      // (the original's text came first, and stays)
        ASSERT_EQ(count_of(transcript(), "News Flash: " + has_teammate), 1u);
        duo.step(3000);
        ASSERT_EQ(count_of(transcript(), has_teammate), 1u);                                       // once
        app.net()->freeze();
        duo.step(3000);
        ASSERT_EQ(app.sim().current_tick(), bob.sim.current_tick());
        ASSERT_TRUE(app.sim().state_hash() == bob.sim.state_hash());
        ASSERT_FALSE(app.net()->desynced() || bob.net.desynced());
        app.quit();
        app.return_to_map_select();
    } TEST_END();
}

void run_host_tests() {
    TEST_CASE("N5.3 Host: The Setup Screen Is The Room (names, thumbs, the original's prompt); START Loads Everywhere; The Match Runs On The Lock-Step Ticks") {
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Host;
        cfg.net_port = 0;
        cfg.net_loopback_only = true;
        cfg.player_name = "Alice";
        Application app;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_TRUE(app.network_active());
        ASSERT_TRUE(app.net()->is_host());
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_TRUE(app.net()->listen_port() != 0);
        // alone in the room: one row, the original's prompt, START refused with the can't-go cue (nobody to play with)
        app.pump_network(0.01f);
        ASSERT_TRUE(app.map_select().room().networked && app.map_select().room().is_host);
        ASSERT_TRUE(app.map_select().room().seats[0].occupied && app.map_select().room().seats[0].name == "Alice");
        ASSERT_FALSE(app.map_select().room().seats[1].occupied);
        ASSERT_EQ(app.map_select().room().status, std::string(sim::strings::text(sim::strings::kPressStart)));
        app.map_select().handle_key_down(SDLK_RETURN);
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_EQ(app.net()->phase(), net::NetGame::Phase::Room);

        Peer bob;
        ASSERT_TRUE(bob.net.join("127.0.0.1", app.net()->listen_port(), "Bob"));
        Duo duo{app, bob};
        ASSERT_TRUE(duo.until([&]() { return app.net()->can_start(); }, 8000));
        const auto& room = app.map_select().room();
        ASSERT_TRUE(room.seats[1].occupied && room.seats[1].name == "Bob");
        ASSERT_TRUE(room.seats[0].thumb == MapSelectScreen::Thumb::Good && room.seats[1].thumb == MapSelectScreen::Thumb::Good);
        ASSERT_EQ(bob.net.room().slots[0].name, "Alice");
        // the host picks the map and the fog through the setup screen's controls; the guest's room follows
        int32_t small_index = -1;
        for (size_t i = 0; i < app.map_select().get_maps().size(); ++i) {
            if (app.map_select().get_maps()[i].filename == "SMALL.LVL") small_index = static_cast<int32_t>(i);
        }
        ASSERT_TRUE(small_index >= 0);
        app.map_select().set_selected_index(small_index);
        const int32_t fx = MapSelectScreen::BTN_FOW_ON_X + 2;
        const int32_t fy = MapSelectScreen::BTN_FOW_ON_Y + 2;
        app.map_select().handle_mouse_motion(fx, fy);                                   // the button acts at the release
        app.map_select().handle_mouse_down(fx, fy, 1);
        app.map_select().handle_mouse_up(fx, fy, 1);
        duo.step(200);
        ASSERT_EQ(bob.net.room().map_name, "SMALL.LVL");
        ASSERT_TRUE(bob.net.room().fog);
        // START
        app.map_select().handle_key_down(SDLK_RETURN);
        ASSERT_TRUE(duo.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 8000));
        ASSERT_EQ(app.local_player_id(), 0);
        ASSERT_EQ(app.sim().roster_mask(), 0x03);
        ASSERT_TRUE(app.sim().is_fog_of_war_enabled());
        ASSERT_EQ(app.sim().grid().anthills().size(), 2u);
        ASSERT_EQ(app.sim().get_player_name(0), "Alice");
        ASSERT_EQ(app.sim().get_player_name(1), "Bob");
        ASSERT_EQ(app.hud().team_names()[1], "Bob");
        ASSERT_EQ(app.hud().roster_mask(), 0x03);
        ASSERT_TRUE(app.hud().is_modal_open());                                          // "Get ready" for at least 5 s
        ASSERT_TRUE(app.sim().state_hash() == bob.sim.state_hash());

        // play: orders through the HUD's own sink, both ways, and chat
        int predicted = 0;
        int given = 0;
        uint32_t next = 0;
        for (uint32_t t = 0; t < 30000 + kDialogMs; t += 10) {                           // (the host seals the first turn kDialogMs after the match began: 30 s of play follow)
            if (t >= next) {
                next = t + 900;
                const auto mine = ants_of(app.sim(), 0);
                const auto theirs = ants_of(bob.sim, 1);
                if (!mine.empty()) {
                    const sim::CommandResult r = app.net()->submit(order(0, mine[(t / 900) % mine.size()], static_cast<int16_t>((t / 10) % 40), static_cast<int16_t>((t / 25) % 40)));
                    ++given;
                    predicted += r.ack_ant != 0 ? 1 : 0;
                }
                if (!theirs.empty()) bob.net.submit(order(1, theirs[(t / 900) % theirs.size()], static_cast<int16_t>((t / 15) % 40), static_cast<int16_t>((t / 30) % 40)));
            }
            duo.step(10);
        }
        ASSERT_TRUE(given > 20 && predicted > given / 2);
        ASSERT_FALSE(app.hud().is_modal_open());                                         // the ticks reached the HUD (post_tick), the modal ended
        ASSERT_TRUE(app.net()->turns_executed() > 500);                                  // (turns of 50 ms: the 250 of 100 ms that this stood for, twice)
        // chat both ways
        app.hud().set_chat_input("hello Bob");
        app.hud().send_chat(false);
        bob.net.chat("hello Alice", false);
        duo.step(1000);
        bool host_text = false;
        for (const auto& c : bob.chats) host_text = host_text || (c.text == "hello Bob" && c.sender == 0);
        ASSERT_TRUE(host_text);
        ASSERT_TRUE(log_has(app.hud().get_chat_log(), "Bob:") && log_has(app.hud().get_chat_log(), "hello Alice"));
        // a frame renders with the network overlay code path
        app.render_frame();
        // the guest leaves: its team drops on the host at the same tick, the host plays on
        bob.net.leave();
        duo.step(3000);
        ASSERT_TRUE(app.sim().is_player_dropped(1));
        ASSERT_EQ(app.state(), AppState::Playing);
        ASSERT_TRUE(log_has(app.hud().get_chat_log(), "dropped out of"));
        app.quit();
        ASSERT_FALSE(app.network_active());
    } TEST_END();

    TEST_CASE("N5.90 The site statistics' hook is for a game on this computer only: the same counter that hears a local game hears nothing of a match of the network, on the host that plays it or on a guest, however long it runs (the page counts the online games on the server's side)") {
        {   // the control: a game on this computer is heard
            int told = 0;
            ApplicationConfig cfg = headless_config();
            cfg.default_map_path = "Original-Ants/Maps/TINY.LVL";
            cfg.play_at_once = true;
            Application local;
            local.set_on_local_match_started([&told]() { ++told; });
            ASSERT_TRUE(local.init(cfg));
            ASSERT_TRUE(local.state() == AppState::Playing && !local.network_active());
            local.hud().dismiss_match_start_modal();
            for (int i = 0; i < 10; ++i) local.update_simulation(0.05f);
            ASSERT_EQ(told, 1);
            local.shutdown();
        }
        {   // the host of a room, with a guest: START, the dialog, half a minute of play
            int told = 0;
            ApplicationConfig cfg = headless_config();
            cfg.net_role = ApplicationConfig::NetRole::Host;
            cfg.net_port = 0;
            cfg.net_loopback_only = true;
            cfg.player_name = "Alice";
            Application app;
            app.set_on_local_match_started([&told]() { ++told; });
            ASSERT_TRUE(app.init(cfg));
            Peer bob;
            ASSERT_TRUE(bob.net.join("127.0.0.1", app.net()->listen_port(), "Bob"));
            Duo duo{app, bob};
            ASSERT_TRUE(duo.until([&]() { return app.net()->can_start(); }, 8000));
            app.map_select().handle_key_down(SDLK_RETURN);
            ASSERT_TRUE(duo.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 8000));
            duo.step(kDialogMs + 3000);
            ASSERT_TRUE(app.net()->turns_executed() > 20 && app.sim().current_tick() > 20);       // the match ran
            ASSERT_EQ(told, 0);
            app.quit();
        }
        {   // a guest of a room (the application is seat 2 of three): the same
            int told = 0;
            Peer host;
            Peer bob;
            Application app;
            app.set_on_local_match_started([&told]() { ++told; });
            ASSERT_TRUE(start_three(host, bob, app, "Cat"));
            Trio trio{app, host, bob};
            trio.step(kDialogMs + 3000);
            ASSERT_TRUE(app.net()->turns_executed() > 20 && app.sim().current_tick() > 20);
            ASSERT_EQ(told, 0);
            app.quit();
        }
    } TEST_END();

    TEST_CASE("N5.3b Host: before the host chooses anything its room is on the map that its setup screen highlights, TREASURE.LVL, and the guest's screen shows it (the host's own choice still wins, Down from the last map wraps to the first); a Maps folder without TREASURE.LVL gives the first map of its list") {
        {
            ApplicationConfig cfg = headless_config();
            cfg.net_role = ApplicationConfig::NetRole::Host;
            cfg.net_port = 0;
            cfg.net_loopback_only = true;
            cfg.player_name = "Alice";
            Application app;
            ASSERT_TRUE(app.init(cfg));
            const MapSelectScreen& screen = app.map_select();
            ASSERT_EQ(screen.get_maps()[static_cast<size_t>(screen.get_selected_index())].filename, "TREASURE.LVL");
            ASSERT_EQ(app.net()->room().map_name, "TREASURE.LVL");                          // the room is on it from the start: START needs a chosen map, and this is the host's
            Peer bob;
            ASSERT_TRUE(bob.net.join("127.0.0.1", app.net()->listen_port(), "Bob"));
            Duo duo{app, bob};
            ASSERT_TRUE(duo.until([&]() { return bob.net.phase() == net::NetGame::Phase::Room && bob.net.room().map_name == "TREASURE.LVL"; }, 8000));
            app.map_select().handle_key_down(SDLK_DOWN);                                      // TREASURE.LVL is the last map of the list: Down wraps to the first
            duo.step(200);
            ASSERT_EQ(bob.net.room().map_name, "GAUNTLET.LVL");
            app.quit();
        }
        {   // a folder that has no TREASURE.LVL (here two copies of TINY): the first map of its list, as before
            namespace fs = std::filesystem;
            std::random_device unique;
            const fs::path folder = fs::temp_directory_path() / ("ants_host_maps_test_" + std::to_string(unique()));
            std::error_code ec;
            fs::create_directories(folder, ec);
            for (const char* name : {"TINY.LVL", "OCEAN.LVL"}) fs::copy_file(maps_dir() + "TINY.LVL", folder / name, fs::copy_options::overwrite_existing, ec);
            ApplicationConfig cfg = headless_config();
            cfg.net_role = ApplicationConfig::NetRole::Host;
            cfg.net_port = 0;
            cfg.net_loopback_only = true;
            cfg.player_name = "Alice";
            cfg.maps_dir = folder.string();
            {
                Application app;
                const bool ok = app.init(cfg);
                const std::string room_map = ok ? app.net()->room().map_name : std::string();
                if (ok) app.quit();
                fs::remove_all(folder, ec);
                ASSERT_TRUE(ok);
                ASSERT_EQ(room_map, "OCEAN.LVL");
            }
        }
    } TEST_END();
}

void run_guest_tests() {
    TEST_CASE("N5.4 Guest: The Room Shows The Host's Choice And The Original's Waiting Text; Only Leave Works; Playing As Seat 1; Team Switching Is Off") {
        Peer host;
        ASSERT_TRUE(host.net.host(0, "Alice", true));
        host.net.set_map("TINY.LVL");                                                    // the application chooses the first map of its list right away; a bare host does the same
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Join;
        cfg.net_address = "127.0.0.1";
        cfg.net_port = host.net.listen_port();
        Application app;                                                                  // no --name: the default in a network game is "Player"
        ASSERT_TRUE(app.init(cfg));
        ASSERT_TRUE(app.network_active());
        ASSERT_FALSE(app.net()->is_host());
        // a guest that is still connecting shows itself alone (the original's slot 0 is the local machine from the start, with the good thumb)
        app.pump_network(0.0f);
        if (app.net()->phase() == net::NetGame::Phase::Connecting) {
            const auto& early = app.map_select().room();
            int shown = 0;
            for (const auto& seat : early.seats) shown += seat.occupied ? 1 : 0;
            ASSERT_TRUE(early.networked && !early.is_host && shown == 1);
            ASSERT_TRUE(early.seats[early.my_seat].occupied && early.seats[early.my_seat].name == "Player" && early.seats[early.my_seat].thumb == MapSelectScreen::Thumb::Good);
        }
        Duo duo{app, host};
        ASSERT_TRUE(duo.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && host.net.can_start(); }, 8000));
        const auto& room = app.map_select().room();
        ASSERT_TRUE(room.networked && !room.is_host && room.my_seat == 1);
        ASSERT_TRUE(room.seats[0].occupied && room.seats[0].name == "Alice" && room.seats[1].occupied && room.seats[1].name == "Player");
        {   // the guest's rows: itself first, then the host (the original's slot 0 is the local machine, slot 1 the host)
            const std::array<int8_t, 4> rows = MapSelectScreen::row_seats(room);
            ASSERT_TRUE(rows[0] == 1 && rows[1] == 0 && rows[2] == -1 && rows[3] == -1);
        }
        ASSERT_EQ(room.status, std::string(sim::strings::text(sim::strings::kWaitingForHost)));
        // the host's choice arrives; the guest's controls do nothing
        host.net.set_map("TINY.LVL");
        host.net.set_fog(true);
        duo.step(300);
        ASSERT_EQ(app.map_select().get_maps()[static_cast<size_t>(app.map_select().get_selected_index())].filename, "TINY.LVL");
        ASSERT_TRUE(app.map_select().is_fog_of_war_enabled());
        ASSERT_EQ(app.map_select().room().map_file, std::string("TINY.LVL"));            // what a guest shows: the host's file name
        const int32_t idx = app.map_select().get_selected_index();
        app.map_select().handle_mouse_motion(MapSelectScreen::BTN_DOWN_X + 3, MapSelectScreen::BTN_DOWN_Y + 3);
        app.map_select().handle_mouse_down(MapSelectScreen::BTN_DOWN_X + 3, MapSelectScreen::BTN_DOWN_Y + 3, 1);
        app.map_select().handle_mouse_up(MapSelectScreen::BTN_DOWN_X + 3, MapSelectScreen::BTN_DOWN_Y + 3, 1);
        app.map_select().handle_key_down(SDLK_DOWN);
        app.map_select().handle_key_down(SDLK_RETURN);
        app.map_select().handle_key_down(SDLK_s);
        ASSERT_EQ(app.map_select().get_selected_index(), idx);
        ASSERT_TRUE(app.map_select().is_fog_of_war_enabled());
        ASSERT_EQ(app.state(), AppState::MapSelect);
        // the host starts a two-player match on the 31 x 31 map
        uint64_t hash = 0;
        ASSERT_TRUE(net::hash_file(maps_dir() + "TINY.LVL", hash));
        ASSERT_TRUE(host.net.start_match(31337, hash));
        ASSERT_TRUE(duo.until([&]() { return app.state() == AppState::Playing && host.net.phase() == net::NetGame::Phase::Playing; }, 8000));
        ASSERT_EQ(app.local_player_id(), 1);
        ASSERT_EQ(app.sim().roster_mask(), 0x03);
        ASSERT_EQ(app.sim().get_player_name(0), "Alice");
        ASSERT_EQ(app.sim().get_player_name(1), "Player");
        ASSERT_EQ(app.hud().get_player_name(), "Player");
        ASSERT_TRUE(app.sim().state_hash() == host.sim.state_hash());
        // Ctrl+1 does nothing (the original has no team switch key); in a network game the player is its seat in any case
        SDL_KeyboardEvent key{};
        key.type = SDL_KEYDOWN;
        key.keysym.sym = SDLK_1;
        key.keysym.mod = KMOD_CTRL;
        app.handle_key_down(key);
        ASSERT_EQ(app.local_player_id(), 1);
        // play 20 s: the guest's orders go through the host and come back in the turns
        uint32_t next = 0;
        for (uint32_t t = 0; t < 20000; t += 10) {
            if (t >= next) {
                next = t + 700;
                const auto mine = ants_of(app.sim(), 1);
                if (!mine.empty()) app.net()->submit(order(1, mine[(t / 700) % mine.size()], static_cast<int16_t>((t / 10) % 31), static_cast<int16_t>((t / 20) % 31)));
                const auto theirs = ants_of(host.sim, 0);
                if (!theirs.empty()) host.net.submit(order(0, theirs[(t / 700) % theirs.size()], static_cast<int16_t>((t / 12) % 31), static_cast<int16_t>((t / 24) % 31)));
            }
            duo.step(10);
        }
        host.net.freeze();
        duo.step(3000);
        ASSERT_TRUE(app.sim().state_hash() == host.sim.state_hash());                    // the whole pipeline, bit-identical
        ASSERT_FALSE(app.net()->desynced());
        ASSERT_EQ(app.sim().current_tick(), host.sim.current_tick());
        // the host leaves: the only guest takes over (host migration) and the old host's team drops out; no team is left besides the guest, so the drop-out
        // decides the match at once (0x100d172) and the results screen opens
        host.net.leave();
        ASSERT_TRUE(duo.until([&]() { return app.net()->is_host(); }, 8000));
        ASSERT_TRUE(app.network_active());
        ASSERT_EQ(app.state(), AppState::Playing);
        ASSERT_EQ(app.net()->match_notice(), "You are the host now.");
        app.render_frame();                                                               // the overlay draws the notice
        duo.step(3000);
        ASSERT_TRUE(app.sim().is_player_dropped(0));
        ASSERT_FALSE(app.sim().is_player_dropped(1));
        ASSERT_TRUE(app.sim().is_match_over());
        ASSERT_TRUE(app.scorecard().is_open());
        // the player leaves the match: back to the local setup screen
        app.return_to_map_select();
        ASSERT_FALSE(app.network_active());
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_FALSE(app.map_select().room().networked);
        ASSERT_TRUE(app.map_select().can_change_setup());                                 // a local setup screen again
    } TEST_END();

    TEST_CASE("N5.5 Guest: A Machine That Cannot Load The Map Says So In The Original's Words And Cancels The Start For Everybody") {
        Peer host;
        host.check_hash = false;                                                          // the host loads its own file fine; only the guest disagrees
        ASSERT_TRUE(host.net.host(0, "Alice", true));
        host.net.set_map("TINY.LVL");                                                    // the application chooses the first map of its list right away; a bare host does the same
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Join;
        cfg.net_address = "127.0.0.1";
        cfg.net_port = host.net.listen_port();
        cfg.player_name = "Bob";
        Application app;
        ASSERT_TRUE(app.init(cfg));
        Duo duo{app, host};
        ASSERT_TRUE(duo.until([&]() { return host.net.can_start(); }, 8000));
        // a map that this machine's list does not know: the host asks for a file named like a map that is not there
        const uint64_t bogus_hash = 0x1234;
        host.net.set_map("SMALL.LVL");
        duo.step(200);
        ASSERT_TRUE(host.net.start_match(1, bogus_hash));                               // the guest's file has another hash
        ASSERT_TRUE(duo.until([&]() { return host.saw(net::NetGame::Event::Type::Cancelled); }, 8000));
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_EQ(app.net()->phase(), net::NetGame::Phase::Room);
        ASSERT_TRUE(app.map_select().room().status.find("SMALL.LVL") != std::string::npos);
        ASSERT_TRUE(host.net.status_text().find("Bob") != std::string::npos);
    } TEST_END();

    TEST_CASE("N5.6 Guest: When The Host Leaves A Three-Player Match The Application Follows The Lowest Other Seat, Says So, And Stays Identical To The New Host") {
        Peer host;
        ASSERT_TRUE(host.net.host(0, "Alice", true));
        host.net.set_map("TINY.LVL");                                                    // the application chooses the first map of its list right away; a bare host does the same
        Peer bob;
        ASSERT_TRUE(bob.net.join("127.0.0.1", host.net.listen_port(), "Bob"));
        for (int i = 0; i < 800 && bob.net.phase() != net::NetGame::Phase::Room; ++i) {          // Bob is seated first (seat 1), the application second
            host.now += 10;
            host.update();
            bob.now += 10;
            bob.update();
            std::this_thread::sleep_for(std::chrono::microseconds(300));
        }
        ASSERT_TRUE(bob.net.phase() == net::NetGame::Phase::Room && bob.net.my_seat() == 1);
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Join;
        cfg.net_address = "127.0.0.1";
        cfg.net_port = host.net.listen_port();
        cfg.player_name = "Carol";
        Application app;
        ASSERT_TRUE(app.init(cfg));
        Trio trio{app, host, bob};
        ASSERT_TRUE(trio.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && app.net()->my_seat() == 2 && host.net.can_start(); }, 8000));
        host.net.set_map("SMALL.LVL");
        trio.step(200);
        uint64_t hash = 0;
        ASSERT_TRUE(net::hash_file(maps_dir() + "SMALL.LVL", hash));
        ASSERT_TRUE(host.net.start_match(77, hash));
        ASSERT_TRUE(trio.until([&]() {
            return app.state() == AppState::Playing && host.net.phase() == net::NetGame::Phase::Playing && bob.net.phase() == net::NetGame::Phase::Playing;
        }, 8000));
        ASSERT_EQ(app.local_player_id(), 2);
        trio.step(3000);
        host.net.leave();
        ASSERT_TRUE(trio.until([&]() { return bob.net.is_host() && app.net()->host_seat() == 1; }, 10000));
        ASSERT_EQ(app.state(), AppState::Playing);
        ASSERT_TRUE(app.network_active());
        ASSERT_FALSE(app.net()->is_host());
        ASSERT_EQ(app.net()->match_notice(), "Bob is the host now.");
        app.render_frame();                                                               // the overlay draws the notice
        // the game goes on with the two of them: the old host is dropped everywhere, an order of the application reaches Bob's simulation
        const uint32_t before = app.net()->turns_executed();
        trio.step(2000);
        ASSERT_TRUE(app.net()->turns_executed() > before + 20);                      // 2 s: forty turns of 50 ms
        ASSERT_TRUE(app.sim().is_player_dropped(0) && bob.sim.is_player_dropped(0));
        ASSERT_FALSE(app.sim().is_player_dropped(1) || app.sim().is_player_dropped(2));
        const auto mine = ants_of(app.sim(), 2);
        ASSERT_FALSE(mine.empty());
        int16_t gx = 0, gy = 0;
        ASSERT_TRUE(open_goal_near_hill(app.sim(), 2, gx, gy));
        app.net()->submit(order(2, mine[0], gx, gy));
        trio.step(2000);
        ASSERT_EQ(bob.sim.get_unit(mine[0]).orig_order, sim::AntUnit::kOrderMove);
        bob.net.freeze();
        trio.step(3000);
        ASSERT_TRUE(app.sim().state_hash() == bob.sim.state_hash());
        ASSERT_FALSE(app.net()->desynced());
    } TEST_END();

    TEST_CASE("N5.11 Quit Over The Network: The Guest Quits A Two-Player Match (Ctrl+Q, Y): The Quit Command Ends It On Both Machines With The Guest As Quitter, The Results Open, Both Stay Identical") {
        Peer host;
        ASSERT_TRUE(host.net.host(0, "Alice", true));
        host.net.set_map("TINY.LVL");                                                    // the application chooses the first map of its list right away; a bare host does the same
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Join;
        cfg.net_address = "127.0.0.1";
        cfg.net_port = host.net.listen_port();
        cfg.player_name = "Bob";
        Application app;
        ASSERT_TRUE(app.init(cfg));
        Duo duo{app, host};
        ASSERT_TRUE(duo.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && host.net.can_start(); }, 8000));
        host.net.set_map("TINY.LVL");
        duo.step(300);
        uint64_t hash = 0;
        ASSERT_TRUE(net::hash_file(maps_dir() + "TINY.LVL", hash));
        ASSERT_TRUE(host.net.start_match(31337, hash));
        ASSERT_TRUE(duo.until([&]() { return app.state() == AppState::Playing && host.net.phase() == net::NetGame::Phase::Playing; }, 8000));
        ASSERT_EQ(app.local_player_id(), 1);
        duo.step(6000);                                                                   // the "get ready" dialog takes every key for its five seconds
        ASSERT_FALSE(app.hud().is_modal_open());
        ASSERT_FALSE(app.sim().is_match_over());
        ASSERT_EQ(app.sim().other_sides(1), 1u);                                          // two teams: the host is the one other side
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
        ASSERT_TRUE(app.is_running());                                                    // the quit is the end of the match, the results follow
        ASSERT_TRUE(duo.until([&]() { return app.sim().is_match_over() && host.sim.is_match_over(); }, 4000));
        ASSERT_EQ(app.sim().quitter(), 1);
        ASSERT_EQ(host.sim.quitter(), 1);
        ASSERT_FALSE(app.sim().is_player_dropped(1));
        ASSERT_TRUE(app.scorecard().is_open());
        ASSERT_TRUE(app.sim().state_hash() == host.sim.state_hash());
        ASSERT_TRUE(host.sim.get_world_state().match_result.is_winner(0));                // the host's screen plays the winner cue, the quitter's the other
        ASSERT_FALSE(app.sim().get_world_state().match_result.is_winner(1));
        ASSERT_FALSE(app.net()->desynced());
    } TEST_END();

    TEST_CASE("N5.9 Teaming Over The Network: An Offer Reaches The Application As A Question, Accept Makes The Team Everywhere, Team Chat Reaches Only Allies, In The Sender's Colour") {
        Peer host;
        Peer bob;
        Application app;
        ASSERT_TRUE(start_three(host, bob, app, "Carol"));
        Trio trio{app, host, bob};
        ViewportCamera& camera = app.renderer().camera();
        // The match opens with its "Get ready to play!" dialog and the host seals the first turn kDialogMs later; a command that reaches the host BEFORE that is discarded (protocol 12: no honest client sends one,
        // its dialog takes every click and key until its own first turn has run). So the offers of this test wait for the first tick of every machine, as a person's would: Bob and the host are bare
        // machines with no dialog, and the application's HUD is asked directly (a click would be swallowed by the dialog until then).
        ASSERT_TRUE(trio.until([&]() { return app.sim().current_tick() > 0 && bob.sim.current_tick() > 0 && host.sim.current_tick() > 0; }, 8000));
        trio.step(1000);
        // a team message of Alice before there is any team: the application is not her ally and never sees it
        host.net.chat("secret plan", true);
        trio.step(600);
        ASSERT_FALSE(log_has(app.hud().get_chat_log(), "secret plan"));
        // Bob offers the application's team a team: the offer goes through the turns and arrives as the original's question
        Command invite;
        invite.type = CommandType::AllianceInvite;
        invite.other_player = 2;
        ASSERT_EQ(bob.net.submit(invite).status, sim::CommandResult::Status::Applied);
        ASSERT_TRUE(trio.until([&]() { return app.hud().alliance_dialog() == HUD::AllianceDialog::Invitation; }, 5000));
        ASSERT_EQ(app.hud().alliance_dialog_team(), 1);
        ASSERT_EQ(app.hud().alliance_dialog_text(), "Bob (Red) invites you to form a team.  Would you like to accept?");
        ASSERT_TRUE(app.hud().is_modal_open());
        // Accept (A): the answer is a command of the application's seat; every machine makes the team at the same turn
        app.hud().handle_key_down('a', app.sim(), camera);
        ASSERT_EQ(app.hud().alliance_dialog(), HUD::AllianceDialog::None);
        ASSERT_TRUE(trio.until([&]() {
            return app.sim().stats_manager().are_allies(1, 2) && bob.sim.stats_manager().are_allies(1, 2) && host.sim.stats_manager().are_allies(1, 2);
        }, 5000));
        trio.step(300);
        ASSERT_EQ(app.hud().alliance_dialog(), HUD::AllianceDialog::None);                // the question does not come back
        ASSERT_TRUE(log_joined(app.hud().get_chat_log()).find("Bob (Red) and Carol (Blue) are a team now!") != std::string::npos);   // the news flash, wrapped by the chat box
        // Bob's team message reaches his ally in his team's colour (red, index 2); Alice's, sent to her team, still does not
        bob.net.chat("we are allies", true);
        host.net.chat("secret plan two", true);
        trio.step(800);
        const auto& log = app.hud().get_chat_log();
        size_t header = log.size();
        for (size_t i = 0; i < log.size(); ++i) {
            if (log[i] == "Bob (To Teammate):") header = i;
        }
        ASSERT_TRUE(header < log.size());
        ASSERT_EQ(app.hud().get_chat_line_colour(header), 2);
        ASSERT_TRUE(log_has(log, "we are allies"));
        ASSERT_FALSE(log_has(log, "secret plan"));
        // the application's own team message goes through the host to its ally (and nobody else: the host, Alice, is not an ally) and is marked as a team message
        app.hud().set_chat_input("hi Bob");
        app.hud().send_chat(true);
        trio.step(800);
        bool bob_got = false;
        for (const auto& c : bob.chats) bob_got = bob_got || (c.text == "hi Bob" && c.team && c.sender == 2);
        ASSERT_TRUE(bob_got);
        bool alice_got = false;                                                           // (the host relays; it does not hear what it was not sent either)
        for (const auto& c : host.chats) alice_got = alice_got || c.text == "hi Bob";
        ASSERT_FALSE(alice_got);
        host.net.freeze();
        trio.step(3000);
        ASSERT_TRUE(app.sim().state_hash() == bob.sim.state_hash() && app.sim().state_hash() == host.sim.state_hash());
        ASSERT_FALSE(app.net()->desynced());
    } TEST_END();

    TEST_CASE("N5.10 Teaming Over The Network: The Application Asks And Waits; A Refusal Closes The Waiting Dialog, Withdraw (W) Takes The Offer Back Everywhere") {
        Peer host;
        Peer bob;
        Application app;
        ASSERT_TRUE(start_three(host, bob, app, "Carol"));
        Trio trio{app, host, bob};
        ViewportCamera& camera = app.renderer().camera();
        // The match opens with its "Get ready to play!" dialog and the host seals the first turn kDialogMs later; a command that reaches the host BEFORE that is discarded (protocol 12: no honest client sends one,
        // its dialog takes every click and key until its own first turn has run). So the offers of this test wait for the first tick of every machine, as a person's would: Bob and the host are bare
        // machines with no dialog, and the application's HUD is asked directly (a click would be swallowed by the dialog until then).
        ASSERT_TRUE(trio.until([&]() { return app.sim().current_tick() > 0 && bob.sim.current_tick() > 0 && host.sim.current_tick() > 0; }, 8000));
        trio.step(1000);
        app.hud().request_team_up(app.sim(), 1);                                          // the ally pedestal of Bob's hill
        ASSERT_TRUE(trio.until([&]() { return app.hud().alliance_dialog() == HUD::AllianceDialog::Waiting; }, 5000));
        ASSERT_EQ(app.hud().alliance_dialog_team(), 1);
        ASSERT_EQ(app.hud().alliance_dialog_text(), "Waiting for Bob (Red) to respond to your offer to team up.");
        ASSERT_EQ(bob.sim.get_world_state().pending_invite_from[1], 2);                   // Bob's simulation holds the offer as well
        // Bob refuses: the waiting dialog closes, the status line says so
        Command deny;
        deny.type = CommandType::AllianceDeny;
        deny.other_player = 2;
        ASSERT_EQ(bob.net.submit(deny).status, sim::CommandResult::Status::Applied);
        ASSERT_TRUE(trio.until([&]() { return app.hud().alliance_dialog() == HUD::AllianceDialog::None; }, 5000));
        trio.step(300);
        ASSERT_EQ(app.hud().status_line().text(), "Bob rejected teaming up");
        ASSERT_FALSE(app.sim().stats_manager().are_allies(1, 2));
        ASSERT_EQ(app.hud().alliance_dialog(), HUD::AllianceDialog::None);
        // again, and this time the application takes the offer back with W
        app.hud().request_team_up(app.sim(), 1);
        ASSERT_TRUE(trio.until([&]() { return app.hud().alliance_dialog() == HUD::AllianceDialog::Waiting; }, 5000));
        app.hud().handle_key_down('w', app.sim(), camera);
        ASSERT_EQ(app.hud().alliance_dialog(), HUD::AllianceDialog::None);
        ASSERT_TRUE(trio.until([&]() {
            return app.sim().get_world_state().pending_invite_from[1] == 255 && bob.sim.get_world_state().pending_invite_from[1] == 255 &&
                   host.sim.get_world_state().pending_invite_from[1] == 255;
        }, 5000));
        trio.step(300);
        ASSERT_EQ(app.hud().alliance_dialog(), HUD::AllianceDialog::None);
        host.net.freeze();
        trio.step(3000);
        ASSERT_TRUE(app.sim().state_hash() == bob.sim.state_hash() && app.sim().state_hash() == host.sim.state_hash());
    } TEST_END();

    TEST_CASE("N5.74 The \"Get Ready\" Dialog Of A Guest (A Match On The Local Network, Protocol 12): It Opens When The Match Begins And Ends With The Guest's First Executed Turn, Not Before And Not After (Up At Every 10 ms Step While No Tick Has Run, Gone The Step The First One Ran); In Those 5 s The Portrait Moves, The Clock Shows The Full Time And The Screen Says Nothing (No Waiting Message, No Lag, No Pause); No Click Selects Until It Is Gone") {
        Peer host;
        ASSERT_TRUE(host.net.host(0, "Alice", true));
        host.net.set_map("TINY.LVL");
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Join;
        cfg.net_address = "127.0.0.1";
        cfg.net_port = host.net.listen_port();
        cfg.player_name = "Bob";
        Application app;
        ASSERT_TRUE(app.init(cfg));
        Duo duo{app, host};
        ASSERT_TRUE(duo.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && host.net.can_start(); }, 8000));
        uint64_t hash = 0;
        ASSERT_TRUE(net::hash_file(maps_dir() + "TINY.LVL", hash));
        ASSERT_TRUE(host.net.start_match(4343, hash));
        ASSERT_TRUE(duo.until([&]() { return app.state() == AppState::Playing && host.net.phase() == net::NetGame::Phase::Playing; }, 8000));
        ASSERT_TRUE(app.hud().is_match_start_modal_active() && app.sim().current_tick() == 0 && app.hud().match_start_modal_ticks() <= 1u);
        const uint32_t full_time = app.sim().get_match_time_remaining_ms();
        ASSERT_TRUE(full_time > 0);
        const uint32_t mine = static_cast<uint32_t>(ants_of(app.sim(), 1).front());
        uint32_t waited = 0;
        while (app.sim().current_tick() == 0 && waited < 20000) {
            ASSERT_TRUE(app.hud().is_match_start_modal_active());                              // up exactly while no tick has run ...
            ASSERT_EQ(app.sim().get_match_time_remaining_ms(), full_time);                      // ... the clock shows the match's full time ...
            ASSERT_TRUE(app.net_overlay_now().text.empty());                                    // ... the screen says nothing: no "Waiting for the other players...", no lag, no "Catching up...", no notice ...
            ASSERT_TRUE(app.net()->stalled_ms() == 0 && app.net()->laggard() == 255 && !app.net()->lag_notice() && !app.net()->catching_up() && !app.net()->electing() && !app.net()->desynced());
            if (waited == 2000) {                                                               // ... and a click on an own ant selects nothing
                for (const auto& a : app.sim().get_world_state().ants) {
                    if (a.id != mine) continue;
                    app.renderer().camera().center_on(a.px, a.py, app.sim().grid().width(), app.sim().grid().height());
                    int32_t sx = 0;
                    int32_t sy = 0;
                    ASSERT_TRUE(app.renderer().camera().world_to_screen(a.px, a.py, sx, sy));
                    ASSERT_TRUE(app.hud().handle_mouse_down(sx, sy, 1, app.sim(), app.renderer().camera()));         // (taken by the dialog)
                    app.hud().handle_mouse_up(sx, sy, 1, app.sim(), app.renderer().camera());
                    ASSERT_TRUE(app.hud().get_selected_ant_ids().empty() && app.hud().get_selected_ant_id() == 0u);
                }
            }
            duo.step(10);
            waited += 10;
        }
        ASSERT_TRUE(app.sim().current_tick() >= 1);
        ASSERT_FALSE(app.hud().is_match_start_modal_active());                                  // gone on the very step that the first turn ran
        ASSERT_TRUE(waited >= kDialogMs && waited <= kDialogMs + 250);                          // 5 s, and the link, the second turn (the buffer) and a step
        ASSERT_TRUE(app.hud().match_start_modal_ticks() >= static_cast<uint32_t>(kDialogSteps) - 2u);    // the portrait moved in real time, 50 ms at a time, all the while (the wait is no hang)
        duo.step(3000);
        ASSERT_TRUE(app.sim().get_match_time_remaining_ms() < full_time);                       // the clock runs from the first tick
        ASSERT_TRUE(app.net_overlay_now().text.empty() && app.net()->stalled_ms() == 0);
        host.net.freeze();
        duo.step(2000);
        ASSERT_TRUE(app.sim().state_hash() == host.sim.state_hash() && !app.net()->desynced());
    } TEST_END();
}

// The first player in the room of a dedicated server is its LEADER (protocol 7): its setup screen is the host's, with START, in a "server room" mode
void run_leader_tests() {
    const auto click_start = [](Application& app) {
        const int32_t x = MapSelectScreen::BTN_START_X + 5;
        const int32_t y = MapSelectScreen::BTN_START_Y + 5;
        app.map_select().handle_mouse_motion(x, y);
        app.map_select().handle_mouse_down(x, y, 1);
        app.map_select().handle_mouse_up(x, y, 1);
    };
    const auto join_config = [](const Server& server, const std::string& room, const std::string& name) {
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Join;
        cfg.net_address = "127.0.0.1";
        cfg.net_port = server.port();
        cfg.net_room = room;
        cfg.player_name = name;
        return cfg;
    };
    using Rows = std::array<int8_t, 4>;
    using L = net::FillLevel;
    // the leader's screen (protocol 14): the middle of a player's row, a press on it (a click: down and up), the screen's own 500 ms before it shows its players, the room's notices as the player reads them
    const auto row_centre = [](Application& app, size_t row) {
        const LayoutRect r = app.map_select().player_row_rect(row);
        return std::pair<int32_t, int32_t>(r.x + r.w / 2, r.y + r.h / 2);
    };
    const auto press_row = [&](Application& app, size_t row) {
        const std::pair<int32_t, int32_t> at = row_centre(app, row);
        app.map_select().handle_mouse_motion(at.first, at.second);
        app.map_select().handle_mouse_down(at.first, at.second, 1);
        app.map_select().handle_mouse_up(at.first, at.second, 1);
    };
    const auto settle = [](Application& app) {
        for (int i = 0; i < 6; ++i) app.run_frame_with_delta(0.1f);
    };
    const auto notices_of = [](Application& app) {
        std::vector<std::string> out;
        for (const net::ChatLine& line : app.net()->pregame_chat()) {
            if (line.notice()) out.push_back(line.text);
        }
        return out;
    };
    const auto plan = [](L green, L red, L blue, L black) { return net::FillPlan(std::array<L, 4>{green, red, blue, black}); };

    TEST_CASE("N5.20 Leader: The First Player Of A Server's Room Has The Host's Screen With START (The Room's Map And Fog Shown, Nothing To Change), START With Nobody Else Gives The Can't-Go Cue And Sends Nothing, With A Second Player The Server Starts The Match With Both") {
        Server server;
        ASSERT_TRUE(server.make_room("LEAD-APP", 4));
        Application app;
        ASSERT_TRUE(app.init(join_config(server, "LEAD-APP", "Leader")));
        Peer bob;
        Hall hall{server, &app, {&bob}};
        ASSERT_TRUE(hall.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && app.net()->my_seat() == 0 && app.net()->room().leader == 0; }, 8000));
        // alone: the leader. The screen is the host's (START, Up, Down, Fog) but the setup is the room's, and the prompt is the host's
        ASSERT_TRUE(app.net()->is_leader() && !app.net()->is_host());
        {
            const auto& room = app.map_select().room();
            ASSERT_TRUE(room.networked && !room.is_host && room.leader && room.my_seat == 0);
            ASSERT_FALSE(app.map_select().is_guest());
            ASSERT_TRUE(app.map_select().leads_server_room() && app.map_select().has_start_button());
            ASSERT_FALSE(app.map_select().can_change_setup());                              // the map and the fog are the room's
            ASSERT_EQ(room.status, std::string(sim::strings::text(sim::strings::kPressStart)));
            ASSERT_EQ(room.map_file, std::string("TINY.LVL"));
            ASSERT_EQ(app.map_select().get_maps()[static_cast<size_t>(app.map_select().get_selected_index())].filename, "TINY.LVL");
            ASSERT_TRUE(room.seats[0].occupied && room.seats[0].name == "Leader" && !room.seats[1].occupied);
        }
        // Up, Down, the wrap and the Fog buttons change nothing (and tell nobody)
        {
            const int32_t index = app.map_select().get_selected_index();
            const std::string map_before = app.net()->room().map_name;
            const auto press = [&](int32_t x, int32_t y) {
                app.map_select().handle_mouse_motion(x, y);
                app.map_select().handle_mouse_down(x, y, 1);
                app.map_select().handle_mouse_up(x, y, 1);
            };
            press(MapSelectScreen::BTN_DOWN_X + 3, MapSelectScreen::BTN_DOWN_Y + 3);
            press(MapSelectScreen::BTN_UP_X + 3, MapSelectScreen::BTN_UP_Y + 3);
            press(MapSelectScreen::BTN_FOW_ON_X + 2, MapSelectScreen::BTN_FOW_ON_Y + 2);
            for (int i = 0; i < 12; ++i) {                                                  // (more than the six maps: the wrap)
                app.map_select().handle_key_down(SDLK_DOWN);
                app.map_select().handle_key_down(SDLK_UP);
            }
            app.map_select().handle_key_down(SDLK_UP);
            ASSERT_EQ(app.map_select().get_selected_index(), index);
            ASSERT_FALSE(app.map_select().is_fog_of_war_enabled());
            hall.step(300);
            ASSERT_EQ(app.net()->room().map_name, map_before);
            ASSERT_FALSE(app.net()->room().fog);
            ASSERT_EQ(server.status("LEAD-APP").map, std::string("TINY.LVL"));
            ASSERT_FALSE(server.status("LEAD-APP").fog);
        }
        // START with nobody to play with: the host's answer to START with too few players, the can't-go cue, and nothing else
        {
            const size_t channels = app.audio_mixer().active_channel_count();
            app.map_select().handle_key_down(SDLK_RETURN);
            ASSERT_EQ(app.audio_mixer().active_channel_count(), channels + 1);              // the cue (sound 63)
            click_start(app);
            ASSERT_EQ(app.audio_mixer().active_channel_count(), channels + 3);              // the click on the pressed button and the cue again
            hall.step(500);
            ASSERT_EQ(app.state(), AppState::MapSelect);
            ASSERT_FALSE(app.map_select().is_locked());
            const server::RoomStatus s = server.status("LEAD-APP");
            ASSERT_TRUE(s.state == server::RoomState::Waiting && s.ignored_start_requests == 0);       // nothing was sent: the client knows
        }
        // a second player: the leader keeps the screen, the other one has the guest's
        ASSERT_TRUE(bob.net.join("127.0.0.1", server.port(), "Bob", 255, "LEAD-APP"));
        ASSERT_TRUE(hall.until([&]() { return bob.net.phase() == net::NetGame::Phase::Room && app.net()->room().slots[1].state == net::SlotState::Client; }, 8000));
        ASSERT_EQ(app.map_select().room().status, std::string("Tap a player to change their colour."));            // (the leader's first company: how to change a colour, for five seconds; looked at as soon as he is there)
        ASSERT_TRUE(hall.until([&]() { return app.net()->room().slots[1].rtt_ms != net::kRttUnknown; }, 8000));
        ASSERT_TRUE(app.net()->is_leader() && app.map_select().leads_server_room());
        ASSERT_FALSE(bob.net.is_leader());
        ASSERT_FALSE(bob.net.request_start());                                              // a guest sends nothing
        ASSERT_EQ(bob.net.room().leader, 0);
        ASSERT_EQ(bob.net.status_text(), std::string(sim::strings::text(sim::strings::kWaitingForHost)));
        hall.step(5000);
        ASSERT_EQ(app.map_select().room().status, std::string(sim::strings::text(sim::strings::kPressStart)));
        ASSERT_TRUE(app.map_select().room().seats[1].occupied && app.map_select().room().seats[1].name == "Bob");
        ASSERT_TRUE(MapSelectScreen::row_seats(app.map_select().room()) == Rows({0, 1, -1, -1}));
        // START: the server starts the match with the two of them (a room for four that is half full)
        ASSERT_EQ(server.status("LEAD-APP").expected, 4);
        bob.hold_report = true;                                                              // Bob's machine is slow to report the map: the room stays in its loading phase
        click_start(app);
        ASSERT_TRUE(hall.until([&]() { return app.net()->phase() == net::NetGame::Phase::Loading && bob.report_pending; }, 10000));
        {   // while the map loads the leader keeps the host's screen, locked: START sends nothing more (a second click, Enter, S), Leave works
            ASSERT_EQ(app.state(), AppState::MapSelect);
            ASSERT_TRUE(app.net()->is_leader() && app.map_select().leads_server_room() && !app.map_select().is_guest());
            ASSERT_TRUE(app.map_select().is_locked());
            click_start(app);
            app.map_select().handle_key_down(SDLK_RETURN);
            app.map_select().handle_key_down(SDLK_s);
            hall.step(300);
            ASSERT_EQ(server.status("LEAD-APP").ignored_start_requests, 0u);                // nothing was sent
            ASSERT_TRUE(server.status("LEAD-APP").state == server::RoomState::Loading);
        }
        bob.hold_report = false;
        bob.release_report();
        ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 10000));
        const server::RoomStatus s = server.status("LEAD-APP");
        ASSERT_TRUE(s.state == server::RoomState::Running && s.joined == 2 && s.expected == 4);
        ASSERT_TRUE(s.names[0] == "Leader" && s.names[1] == "Bob" && s.names[2].empty());
        ASSERT_EQ(app.local_player_id(), 0);
        ASSERT_EQ(app.sim().roster_mask(), 0x03);
        ASSERT_EQ(bob.sim.roster_mask(), 0x03);
        ASSERT_TRUE(app.map_select().is_locked());                                          // the Start locked the screen: nothing more to press
        hall.step(5000);
        ASSERT_TRUE(hall.identical(app.sim(), bob.sim));
        ASSERT_FALSE(app.net()->desynced() || bob.net.desynced());
        ASSERT_TRUE(server.status("LEAD-APP").state == server::RoomState::Running);          // the referee saw no desync
        app.quit();
        ASSERT_FALSE(app.network_active());
    } TEST_END();

    TEST_CASE("N5.21 Leader: A Player Who Is Not The Leader Keeps The Guest's Screen (START Does Nothing); When The Leader Leaves The Next One Gets The Host's Screen At The Next Room Message And Starts The Match With The Players Who Are There") {
        Server server;
        ASSERT_TRUE(server.make_room("LEAD-SWITCH", 4));
        Peer ann;                                                                           // the first to join: the leader
        ASSERT_TRUE(ann.net.join("127.0.0.1", server.port(), "Ann", 255, "LEAD-SWITCH"));
        Hall hall{server, nullptr, {&ann}};
        ASSERT_TRUE(hall.until([&]() { return ann.net.phase() == net::NetGame::Phase::Room && ann.net.is_leader(); }, 8000));
        ASSERT_EQ(ann.net.status_text(), std::string(sim::strings::text(sim::strings::kPressStart)));       // the leader has the host's prompt
        ASSERT_FALSE(ann.net.request_start());                                              // alone in the room: nobody to play with, nothing is sent
        hall.step(300);
        ASSERT_TRUE(server.status("LEAD-SWITCH").ignored_start_requests == 0 && server.status("LEAD-SWITCH").state == server::RoomState::Waiting);
        Application app;
        ASSERT_TRUE(app.init(join_config(server, "LEAD-SWITCH", "Second")));
        Peer cat;
        hall.app = &app;
        hall.peers.push_back(&cat);
        ASSERT_TRUE(hall.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && app.net()->my_seat() == 1 && app.net()->room().leader == 0; }, 8000));
        // not the leader: the guest's screen, the guest's prompt, nothing to press
        ASSERT_FALSE(app.net()->is_leader());
        {
            const auto& room = app.map_select().room();
            ASSERT_TRUE(room.networked && !room.is_host && !room.leader && room.my_seat == 1);
            ASSERT_TRUE(app.map_select().is_guest() && !app.map_select().leads_server_room() && !app.map_select().has_start_button());
            ASSERT_EQ(room.status, std::string(sim::strings::text(sim::strings::kWaitingForHost)));
            ASSERT_TRUE(MapSelectScreen::row_seats(room) == Rows({1, 0, -1, -1}));          // itself first, then the other
            app.map_select().handle_key_down(SDLK_RETURN);
            app.map_select().handle_key_down(SDLK_s);
            click_start(app);
            hall.step(1000);
            const server::RoomStatus s = server.status("LEAD-SWITCH");
            ASSERT_TRUE(s.state == server::RoomState::Waiting && s.ignored_start_requests == 0 && s.joined == 2);      // nothing was sent
            ASSERT_EQ(app.state(), AppState::MapSelect);
        }
        // the leader leaves: the application is the earliest player who is left, and the screen changes with the next Room message
        ann.net.leave();
        ASSERT_TRUE(hall.until([&]() { return app.net()->is_leader(); }, 8000));
        ASSERT_EQ(app.net()->room().leader, 1);
        hall.step(100);
        {
            const auto& room = app.map_select().room();
            ASSERT_TRUE(room.leader && app.map_select().leads_server_room() && !app.map_select().is_guest() && app.map_select().has_start_button());
            ASSERT_EQ(room.status, std::string(sim::strings::text(sim::strings::kPressStart)));
            ASSERT_TRUE(room.seats[1].occupied && room.seats[1].name == "Second" && !room.seats[0].occupied);
            ASSERT_TRUE(MapSelectScreen::row_seats(room) == Rows({1, -1, -1, -1}));           // itself first, as before: the rows do not jump when the screen changes
            ASSERT_EQ(server.status("LEAD-SWITCH").leader, 1);
            ASSERT_EQ(server.status("LEAD-SWITCH").joined, 1);
        }
        // alone again: START is the can't-go cue, nothing is sent
        app.map_select().handle_key_down(SDLK_RETURN);
        hall.step(500);
        ASSERT_TRUE(server.status("LEAD-SWITCH").state == server::RoomState::Waiting && server.status("LEAD-SWITCH").ignored_start_requests == 0);
        // another player joins (seat 0 is free: it takes it); the leader does not change; Enter starts the match with the two of them
        ASSERT_TRUE(cat.net.join("127.0.0.1", server.port(), "Cat", 255, "LEAD-SWITCH"));
        ASSERT_TRUE(hall.until([&]() { return cat.net.phase() == net::NetGame::Phase::Room && app.net()->room().slots[0].state == net::SlotState::Client; }, 8000));
        ASSERT_TRUE(app.net()->is_leader() && !cat.net.is_leader());
        ASSERT_EQ(cat.net.my_seat(), 0);
        ASSERT_TRUE(MapSelectScreen::row_seats(app.map_select().room()) == Rows({1, 0, -1, -1}));
        app.map_select().handle_key_down(SDLK_RETURN);
        ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing && cat.net.phase() == net::NetGame::Phase::Playing; }, 10000));
        ASSERT_EQ(app.local_player_id(), 1);
        ASSERT_EQ(app.sim().roster_mask(), 0x03);                                           // the seats 0 and 1 (Ann's seat was taken by Cat)
        ASSERT_EQ(cat.sim.roster_mask(), 0x03);
        ASSERT_TRUE(server.status("LEAD-SWITCH").state == server::RoomState::Running && server.status("LEAD-SWITCH").expected == 4);
        hall.step(3000);
        ASSERT_TRUE(hall.identical(app.sim(), cat.sim));
    } TEST_END();

    TEST_CASE("N5.22 Leader: --start-when N (The Front Page's Card And The Headless Test Clients Give It) Presses START For The Leader Of A Server's Room Once N Players Are In (1 To 4 Only: 1 Is For A Leader With --fill-bots; 2 To 4 Only Before Protocol 11); A Game That Does Not Say It Never Does") {
        {   // the command line
            std::vector<std::string> args = {"ants", "--join", "127.0.0.1:4001", "--room", "R-1", "--start-when", "2"};
            std::vector<char*> storage;
            ASSERT_EQ(Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, storage)).net_start_when, 2);
            for (const char* value : {"1", "3", "4"}) {
                args = {"ants", "--start-when", value};
                ASSERT_EQ(Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, storage)).net_start_when, static_cast<uint8_t>(value[0] - '0'));
            }
            for (const char* value : {"0", "5", "-2", "many", ""}) {                         // not a number of players of a match: no hook (1 is one since protocol 11: a person and the bots of a fill)
                args = {"ants", "--start-when", value};
                ASSERT_EQ(Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, storage)).net_start_when, 0);
            }
            args = {"ants", "--start-when"};                                                // no value
            ASSERT_EQ(Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, storage)).net_start_when, 0);
            args = {"ants", "--join", "127.0.0.1:4001"};
            ASSERT_EQ(Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, storage)).net_start_when, 0);       // off by default
        }
        Server server;
        ASSERT_TRUE(server.make_room("LEAD-HOOK", 4));
        ApplicationConfig cfg = join_config(server, "LEAD-HOOK", "Hook");
        cfg.net_start_when = 3;                                                             // the hook presses START when three are in
        Application app;
        ASSERT_TRUE(app.init(cfg));
        Peer bob;
        Peer cat;
        Hall hall{server, &app, {&bob, &cat}};
        ASSERT_TRUE(bob.net.join("127.0.0.1", server.port(), "Bob", 255, "LEAD-HOOK"));
        ASSERT_TRUE(hall.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && app.net()->room().slots[1].state == net::SlotState::Client; }, 8000));
        ASSERT_TRUE(app.net()->is_leader());                                                // (the application joined first)
        hall.step(3000);                                                                    // two are in, the hook wants three: nothing is pressed
        ASSERT_TRUE(server.status("LEAD-HOOK").state == server::RoomState::Waiting && server.status("LEAD-HOOK").ignored_start_requests == 0);
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_TRUE(cat.net.join("127.0.0.1", server.port(), "Cat", 255, "LEAD-HOOK"));
        ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing && cat.net.phase() == net::NetGame::Phase::Playing; }, 15000));
        ASSERT_EQ(app.sim().roster_mask(), 0x07);                                           // the three of them: the room for four started without the fourth
        ASSERT_TRUE(server.status("LEAD-HOOK").state == server::RoomState::Running && server.status("LEAD-HOOK").joined == 3);
        hall.step(3000);
        ASSERT_TRUE(hall.identical(app.sim(), bob.sim) && hall.identical(app.sim(), cat.sim));
    } TEST_END();

    // The quick help and the leader's START (the quick help's START! lies under it). Events go through the application's own loop, one frame per delivery (a headless application
    // stops after ten frames): the headless window is twice the picture, so a picture point (580, 450), on both START buttons, is the window point (1160, 900).
    const auto button_event = [](Uint32 type, Uint8 clicks) {
        SDL_Event e{};
        e.type = type;
        e.button.windowID = SDL_GetWindowID(SDL_GetWindowFromID(1));
        e.button.button = SDL_BUTTON_LEFT;
        e.button.state = type == SDL_MOUSEBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED;
        e.button.clicks = clicks;
        e.button.x = 1160;
        e.button.y = 900;
        SDL_PushEvent(&e);
    };
    const auto key_event = [](SDL_Keycode sym, bool repeat) {
        SDL_Event e{};
        e.type = SDL_KEYDOWN;
        e.key.windowID = SDL_GetWindowID(SDL_GetWindowFromID(1));
        e.key.state = SDL_PRESSED;
        e.key.repeat = repeat ? 1 : 0;
        e.key.keysym.sym = sym;
        e.key.keysym.scancode = SDL_GetScancodeFromKey(sym);
        SDL_PushEvent(&e);
    };

    TEST_CASE("N5.23 Leader: The Quick Help Does Not Press The Leader's START: The Presses That Continue The Click That Closed It (A Second And A Third Click That SDL Counts, A Quick Click That It Does Not Count, Within The Double-Click Time) Close Nothing And Start Nothing; A Click That Begins After That Time Does Start The Match") {
        Server server;
        ASSERT_TRUE(server.make_room("LEAD-QH", 4));
        Application app;
        ASSERT_TRUE(app.init(join_config(server, "LEAD-QH", "Leader")));
        Peer bob;
        Hall hall{server, &app, {&bob}};
        ASSERT_TRUE(hall.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && app.net()->is_leader(); }, 8000));
        ASSERT_TRUE(bob.net.join("127.0.0.1", server.port(), "Bob", 255, "LEAD-QH"));
        ASSERT_TRUE(hall.until([&]() { return bob.net.phase() == net::NetGame::Phase::Room && app.net()->room().slots[1].state == net::SlotState::Client; }, 8000));
        // two players are in: a request of the leader now starts the match, so whatever reaches START shows
        app.finish_loading();
        ASSERT_EQ(app.state(), AppState::QuickHelp);
        button_event(SDL_MOUSEBUTTONDOWN, 1);                                       // the click on START! of the quick help
        button_event(SDL_MOUSEBUTTONUP, 1);
        app.run_frame_with_delta(0.001f);
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_EQ(app.mouse_screen_x(), 580);                                       // (the window point is the picture point (580, 450): on both buttons)
        ASSERT_EQ(app.mouse_screen_y(), 450);
        ASSERT_TRUE(app.map_select().leads_server_room() && app.closing_click_pending());
        ASSERT_FALSE(app.quick_help_start_button().pressed());
        button_event(SDL_MOUSEBUTTONDOWN, 2);                                       // the second click of the double click (SDL counts it): it would press the leader's START
        button_event(SDL_MOUSEBUTTONUP, 2);
        app.run_frame_with_delta(0.001f);
        ASSERT_FALSE(app.map_select().start_button().pressed());
        ASSERT_TRUE(app.map_select().start_button().hovered());                     // (the pointer is on it: a hover, nothing pressed)
        button_event(SDL_MOUSEBUTTONDOWN, 3);                                       // and a third
        button_event(SDL_MOUSEBUTTONUP, 3);
        app.run_frame_with_delta(0.001f);
        SDL_Delay(150);
        button_event(SDL_MOUSEBUTTONDOWN, 1);                                       // a click that SDL does not count as part of the sequence (a hand moves the pointer more than a pixel
        button_event(SDL_MOUSEBUTTONUP, 1);                                         // between the clicks of a double click), 150 ms later: still within the double-click time
        app.run_frame_with_delta(0.001f);
        ASSERT_FALSE(app.map_select().start_button().pressed());
        hall.step(800);
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_FALSE(app.map_select().is_locked());
        ASSERT_TRUE(app.closing_click_pending());                                   // (no new click has begun)
        {
            const server::RoomStatus s = server.status("LEAD-QH");
            ASSERT_TRUE(s.state == server::RoomState::Waiting && s.ignored_start_requests == 0 && s.joined == 2);      // nothing was asked of the server
        }
        SDL_Delay(600);                                                             // the double-click time is over
        button_event(SDL_MOUSEBUTTONDOWN, 2);                                       // (a press that SDL counts as part of a sequence is swallowed whenever it comes: SDL decides that)
        button_event(SDL_MOUSEBUTTONUP, 2);
        app.run_frame_with_delta(0.001f);
        ASSERT_FALSE(app.map_select().start_button().pressed());
        ASSERT_TRUE(app.closing_click_pending());
        button_event(SDL_MOUSEBUTTONDOWN, 1);                                       // a click that begins now: the screen's. START asks, and the server starts the match with the two of them
        button_event(SDL_MOUSEBUTTONUP, 1);
        app.run_frame_with_delta(0.001f);
        ASSERT_FALSE(app.closing_click_pending());
        ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 15000));
        ASSERT_TRUE(server.status("LEAD-QH").state == server::RoomState::Running && server.status("LEAD-QH").joined == 2);
        ASSERT_TRUE(app.map_select().is_locked());
        app.quit();
    } TEST_END();

    TEST_CASE("N5.24 Leader: A Held Key Does Not Press The Leader's START Either: The Enter That Closed The Quick Help (Or S) Repeats Without Effect, A Fresh Click Starts The Match (A Key Leaves No Memory Of A Click); A Click That Began On The Quick Help And Ended After A Key Closed It Starts Nothing") {
        Server server;
        ASSERT_TRUE(server.make_room("LEAD-KEY", 4));
        Application app;
        ASSERT_TRUE(app.init(join_config(server, "LEAD-KEY", "Leader")));
        Peer bob;
        Hall hall{server, &app, {&bob}};
        ASSERT_TRUE(hall.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && app.net()->is_leader(); }, 8000));
        ASSERT_TRUE(bob.net.join("127.0.0.1", server.port(), "Bob", 255, "LEAD-KEY"));
        ASSERT_TRUE(hall.until([&]() { return bob.net.phase() == net::NetGame::Phase::Room && app.net()->room().slots[1].state == net::SlotState::Client; }, 8000));
        app.finish_loading();
        ASSERT_EQ(app.state(), AppState::QuickHelp);
        button_event(SDL_MOUSEBUTTONDOWN, 1);                                       // a press on START! of the quick help that is still held when a key closes the quick help
        app.run_frame_with_delta(0.001f);
        ASSERT_TRUE(app.quick_help_start_button().pressed());
        key_event(SDLK_RETURN, false);                                              // Enter closes it ...
        app.run_frame_with_delta(0.001f);
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_FALSE(app.quick_help_start_button().pressed());                      // (the quick help's button is let go of)
        ASSERT_TRUE(app.map_select().leads_server_room());
        key_event(SDLK_RETURN, true);                                               // ... and is held: its repeats, and the repeats of S
        key_event(SDLK_RETURN, true);
        key_event(SDLK_KP_ENTER, true);
        key_event(SDLK_s, true);
        key_event(SDLK_RETURN, true);
        button_event(SDL_MOUSEBUTTONUP, 1);                                         // and the mouse button of the press that began on the quick help is released over START
        app.run_frame_with_delta(0.001f);
        hall.step(800);
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_FALSE(app.map_select().is_locked());
        {
            const server::RoomStatus s = server.status("LEAD-KEY");
            ASSERT_TRUE(s.state == server::RoomState::Waiting && s.ignored_start_requests == 0 && s.joined == 2);
        }
        ASSERT_FALSE(app.closing_click_pending());                                  // a key that closes the quick help leaves no memory of a click behind
        button_event(SDL_MOUSEBUTTONDOWN, 1);                                       // so a fresh click, here a moment after the key (inside the double-click time), is the screen's:
        button_event(SDL_MOUSEBUTTONUP, 1);                                         // START asks, and the server starts the match
        app.run_frame_with_delta(0.001f);
        ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 15000));
        ASSERT_TRUE(server.status("LEAD-KEY").state == server::RoomState::Running && server.status("LEAD-KEY").joined == 2);
        app.quit();
    } TEST_END();

    TEST_CASE("N5.25 Leader: What The Quick Help Left Behind Does Not Outlive The Setup Screen It Led To: A Click That Closed It Is Remembered Only Until A New Click Begins Or The Setup Screen Is Created Again") {
        Server server;
        ASSERT_TRUE(server.make_room("LEAD-STALE", 4));
        Application app;
        ASSERT_TRUE(app.init(join_config(server, "LEAD-STALE", "Leader")));
        Peer bob;
        Hall hall{server, &app, {&bob}};
        ASSERT_TRUE(hall.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && app.net()->is_leader(); }, 8000));
        ASSERT_TRUE(bob.net.join("127.0.0.1", server.port(), "Bob", 255, "LEAD-STALE"));
        ASSERT_TRUE(hall.until([&]() { return bob.net.phase() == net::NetGame::Phase::Room && app.net()->room().slots[1].state == net::SlotState::Client; }, 8000));
        ASSERT_FALSE(app.closing_click_pending());
        app.finish_loading();
        button_event(SDL_MOUSEBUTTONDOWN, 1);
        button_event(SDL_MOUSEBUTTONUP, 1);
        app.run_frame_with_delta(0.001f);
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_TRUE(app.closing_click_pending());
        key_event(SDLK_RETURN, false);                                              // the match is started with the key: no click comes, the memory of the old one stays
        app.run_frame_with_delta(0.001f);
        ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 15000));
        ASSERT_TRUE(app.closing_click_pending());
        app.return_to_map_select();                                                 // the match is left: the setup screen is created again, and nothing of the quick help is left on it
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_FALSE(app.closing_click_pending());
    } TEST_END();

    TEST_CASE("N5.26 LAN Host: The Original's Own Host Screen Is Not Guarded Against A Double Click, Because The Original Is Not: The Second Click Of A Double Click On START! Presses The Host's START And Starts The Match (A Held Enter Does Not: Its Repeat Is Not A Press, N5.53)") {
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Host;
        cfg.net_port = 0;
        cfg.net_loopback_only = true;
        cfg.player_name = "Alice";
        Application app;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_TRUE(app.network_active() && app.net()->is_host());
        Peer bob;
        ASSERT_TRUE(bob.net.join("127.0.0.1", app.net()->listen_port(), "Bob"));
        Duo duo{app, bob};
        ASSERT_TRUE(duo.until([&]() { return app.net()->room().slots[1].state == net::SlotState::Client && app.net()->can_start(); }, 8000));
        app.finish_loading();
        button_event(SDL_MOUSEBUTTONDOWN, 1);
        button_event(SDL_MOUSEBUTTONUP, 1);
        app.run_frame_with_delta(0.001f);
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_FALSE(app.closing_click_pending());                                  // a host is not a machine that joined a room: nothing is held back
        button_event(SDL_MOUSEBUTTONDOWN, 2);
        app.run_frame_with_delta(0.001f);
        ASSERT_TRUE(app.map_select().start_button().pressed());                     // the second click presses the host's START
        button_event(SDL_MOUSEBUTTONUP, 2);
        app.run_frame_with_delta(0.001f);
        ASSERT_TRUE(duo.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 15000));
        ASSERT_EQ(app.sim().roster_mask(), 0x03);
    } TEST_END();

    // The same at 16:9: the setup screen is the wide screen (the whole 960 x 540 canvas, the headless window is that size: a window point is a canvas point). The quick help is the wide page too:
    // its START! is anchored to the bottom right corner, (849 .. 947, 497 .. 524), and so lies over the leader's START of the wide screen, (846 .. 944, 499 .. 526), 3 px right and 2 px up as the
    // original's own two buttons do (the quick help was a centred page here, with its START! at (740, 480), clear of the leader's START: the closing click is now a click ON that START).
    const auto wide_button = [](Uint32 type, int32_t x, int32_t y, Uint8 clicks) {
        SDL_Event e{};
        e.type = type;
        e.button.windowID = SDL_GetWindowID(SDL_GetWindowFromID(1));
        e.button.button = SDL_BUTTON_LEFT;
        e.button.state = type == SDL_MOUSEBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED;
        e.button.clicks = clicks;
        e.button.x = x;
        e.button.y = y;
        SDL_PushEvent(&e);
    };

    TEST_CASE("N5.26w Leader At 16:9: The Wide Setup Screen Is The Online Variant; The Quick Help's Closing Click Still Does Not Press ITS START (A Click Within The Double-Click Time Is The Rest Of That Gesture Wherever It Falls, And Nothing Is Asked Of The Server); A Click After That Time Asks, And The Server Starts The Match") {
        Server server;
        ASSERT_TRUE(server.make_room("LEAD-WIDE", 4));
        ApplicationConfig cfg = join_config(server, "LEAD-WIDE", "Leader");
        cfg.aspect = Aspect::Wide16x9;
        cfg.aspect_given = true;
        cfg.has_window_size = true;
        cfg.window_w = 960;
        cfg.window_h = 540;
        Application app;
        ASSERT_TRUE(app.init(cfg));
        Peer bob;
        Hall hall{server, &app, {&bob}};
        ASSERT_TRUE(hall.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && app.net()->is_leader(); }, 8000));
        ASSERT_TRUE(bob.net.join("127.0.0.1", server.port(), "Bob", 255, "LEAD-WIDE"));
        ASSERT_TRUE(hall.until([&]() { return bob.net.phase() == net::NetGame::Phase::Room && app.net()->room().slots[1].state == net::SlotState::Client; }, 8000));
        app.run_frame_with_delta(0.001f);
        ASSERT_TRUE(app.map_select().wide_layout() && app.map_select().leads_server_room());
        ASSERT_TRUE(app.map_select().setup_variant() == SetupVariant::Online);
        ASSERT_TRUE(app.map_select().start_button().up_rect() == ButtonRect({846, 499, 98, 27}));
        app.finish_loading();
        app.run_frame_with_delta(0.001f);
        ASSERT_EQ(app.state(), AppState::QuickHelp);
        ASSERT_TRUE(app.picture() == LayoutRect({0, 0, 960, 540}));                 // (the quick help is the whole canvas: its wide page)
        ASSERT_TRUE(app.quick_help_start_button().up_rect() == ButtonRect({849, 497, 98, 27}));
        wide_button(SDL_MOUSEBUTTONDOWN, 895, 510, 1);                              // the click on START! of the quick help (in the bottom right corner)
        wide_button(SDL_MOUSEBUTTONUP, 895, 510, 1);
        app.run_frame_with_delta(0.001f);
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_TRUE(app.picture() == LayoutRect({0, 0, 960, 540}));
        ASSERT_TRUE(app.closing_click_pending());
        ASSERT_TRUE(app.map_select().start_button().hovered());                     // (the pointer is where the click was, over the wide START: a hover, nothing pressed)
        ASSERT_FALSE(app.map_select().start_button().pressed());
        wide_button(SDL_MOUSEBUTTONDOWN, 895, 512, 1);                              // a quick click on the wide START: the rest of the gesture (within the double-click time), swallowed
        wide_button(SDL_MOUSEBUTTONUP, 895, 512, 1);
        app.run_frame_with_delta(0.001f);
        ASSERT_FALSE(app.map_select().start_button().pressed());
        ASSERT_TRUE(app.map_select().start_button().hovered());                     // (the pointer is there: a hover, nothing pressed)
        wide_button(SDL_MOUSEBUTTONDOWN, 895, 512, 2);                              // a second click that SDL counts
        wide_button(SDL_MOUSEBUTTONUP, 895, 512, 2);
        app.run_frame_with_delta(0.001f);
        hall.step(800);
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_FALSE(app.map_select().is_locked());
        {
            const server::RoomStatus s = server.status("LEAD-WIDE");
            ASSERT_TRUE(s.state == server::RoomState::Waiting && s.ignored_start_requests == 0 && s.joined == 2);      // nothing was asked of the server
        }
        SDL_Delay(600);                                                             // the double-click time is over
        wide_button(SDL_MOUSEBUTTONDOWN, 895, 512, 1);                              // a click that begins now: the screen's. START asks, and the server starts the match with the two of them
        wide_button(SDL_MOUSEBUTTONUP, 895, 512, 1);
        app.run_frame_with_delta(0.001f);
        ASSERT_FALSE(app.closing_click_pending());
        ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 15000));
        ASSERT_TRUE(server.status("LEAD-WIDE").state == server::RoomState::Running && server.status("LEAD-WIDE").joined == 2);
        app.quit();
    } TEST_END();

    // Protocol 14: the leader of a server's room puts a player in another colour by pressing the player's row of the Players' Status box (the classic page and the 16:9 one)
    TEST_CASE("N5.91 Leader, The Colours (Protocol 14): A Press On A Player's Row Moves That Player To The Next Free Colour, On The Classic Page And On The 16:9 One; The Plan's Bots Follow, The Player Is Told By The Room, The Leader's Own Row Moves The Leader, A Quick Second Press Waits For The Room's Answer; A Release Off The Row, The Right Button, A Row Of Nobody, A Screen That Has Not Shown Its Players, A Guest's Screen And A Locked One Do Nothing; The Match Starts With The Colours As They Are") {
        const std::string hint = "Tap a player to change their colour.";

        for (const bool wide : {false, true}) {
            Server server;
            const std::string code = wide ? "MOVE-WIDE" : "MOVE-CLASSIC";
            ASSERT_TRUE(server.make_room(code, 4));
            ApplicationConfig cfg = join_config(server, code, "Ann");
            cfg.fill_bots = plan(L::None, L::None, L::Hard, L::None);                       // a Hard bot for Blue: START seats it there while Blue is empty
            if (wide) {
                cfg.aspect = Aspect::Wide16x9;
                cfg.aspect_given = true;
                cfg.has_window_size = true;
                cfg.window_w = 960;
                cfg.window_h = 540;
            }
            Application leader;
            ASSERT_TRUE(leader.init(cfg));
            Hall hall{server, &leader, {}};
            ASSERT_TRUE(hall.until([&]() { return leader.net()->phase() == net::NetGame::Phase::Room && leader.net()->is_leader(); }, 8000));
            leader.run_frame_with_delta(0.001f);
            ASSERT_EQ(leader.state(), AppState::MapSelect);
            ASSERT_EQ(leader.map_select().wide_layout(), wide);
            // the screen shows its players 500 ms after it was made: until then there are no rows to press
            ASSERT_TRUE(!leader.map_select().refreshed() && !leader.map_select().can_move_players());
            press_row(leader, 0);
            ASSERT_TRUE(leader.map_select().pressed_row() == -1 && leader.map_select().hovered_row() == -1);
            hall.step(300);
            ASSERT_TRUE(leader.net()->my_seat() == 0 && server.status(code).seat_moves == 0 && server.status(code).ignored_seat_moves == 0);
            settle(leader);
            ASSERT_TRUE(leader.map_select().refreshed() && leader.map_select().can_move_players());
            ASSERT_TRUE(leader.map_select().room().status != hint);                                    // (alone: nobody to move yet, and nothing is said about it)

            // Gus is the second player: the leader's first company, and the line says how to change a colour (for the five seconds that a notice is shown)
            Application guest;
            ASSERT_TRUE(guest.init(join_config(server, code, "Gus")));
            hall.second = &guest;
            ASSERT_TRUE(hall.until([&]() { return guest.net()->phase() == net::NetGame::Phase::Room && leader.net()->room().slots[1].state == net::SlotState::Client; }, 8000));
            hall.step(30);
            ASSERT_EQ(guest.net()->my_seat(), 1);
            ASSERT_EQ(leader.map_select().room().status, hint);
            ASSERT_EQ(guest.map_select().room().status, std::string(sim::strings::text(sim::strings::kWaitingForHost)));      // (a guest is not told: it cannot move anybody)
            hall.step(5200);
            ASSERT_TRUE(leader.map_select().room().status.find("Blue") != std::string::npos && leader.map_select().room().status.find("Hard") != std::string::npos);        // the START prompt is back
            settle(guest);
            ASSERT_FALSE(guest.map_select().can_move_players());
            ASSERT_TRUE(MapSelectScreen::row_seats(leader.map_select().room()) == Rows({0, 1, -1, -1}));

            // the rows: one box wide and one row high, row after row; a row of nobody and the points around the rows are no rows
            {
                const LayoutRect r0 = leader.map_select().player_row_rect(0);
                const LayoutRect r1 = leader.map_select().player_row_rect(1);
                ASSERT_TRUE(r0.w > 100 && r0.h > 20 && r1.x == r0.x && r1.w == r0.w && r1.h == r0.h && r1.y == r0.bottom());
                ASSERT_TRUE(leader.map_select().player_row_rect(3).bottom() == r0.y + 4 * r0.h);
                // the four rows fill the inside of the black box of the picture and the light stays in it (the classic picture's box measures 200 x 200 from (371, 83) on the screen; the 16:9 page's is its layout's)
                if (wide) {
                    const SetupLayout& layout = SetupLayout::of(leader.map_select().setup_variant());
                    ASSERT_TRUE(r0.x == layout.players_box.x + 4 && r0.w == SetupLayout::kBoxInnerPlayersW && r0.y >= layout.players_box.y + 4);
                    ASSERT_TRUE(leader.map_select().player_row_rect(3).bottom() <= layout.players_box.y + 4 + SetupLayout::kBoxInnerPlayersH);
                } else {
                    ASSERT_TRUE(r0.x == 371 && r0.w == 200 && r0.y == 83 && leader.map_select().player_row_rect(3).bottom() == 283);
                }
                const auto at = [&](size_t row) { return row_centre(leader, row); };
                ASSERT_EQ(leader.map_select().player_row_at(at(0).first, at(0).second), 0);
                ASSERT_EQ(leader.map_select().player_row_at(at(1).first, at(1).second), 1);
                ASSERT_EQ(leader.map_select().player_row_at(at(2).first, at(2).second), -1);               // (nobody is there)
                ASSERT_EQ(leader.map_select().player_row_at(at(3).first, at(3).second), -1);
                ASSERT_EQ(leader.map_select().player_row_at(r0.x, r0.y), 0);                               // (the corners that count)
                ASSERT_EQ(leader.map_select().player_row_at(r1.right() - 1, r1.bottom() - 1), 1);
                ASSERT_EQ(leader.map_select().player_row_at(r0.x - 1, at(0).second), -1);
                ASSERT_EQ(leader.map_select().player_row_at(r0.right(), at(0).second), -1);
                ASSERT_EQ(leader.map_select().player_row_at(at(0).first, r0.y - 1), -1);
                // the pointer lights the row it is over: nobody's row and the space around are not lit
                leader.map_select().handle_mouse_motion(at(1).first, at(1).second);
                ASSERT_EQ(leader.map_select().hovered_row(), 1);
                leader.map_select().handle_mouse_motion(at(2).first, at(2).second);
                ASSERT_EQ(leader.map_select().hovered_row(), -1);
                leader.map_select().handle_mouse_motion(at(0).first, at(0).second);
                ASSERT_EQ(leader.map_select().hovered_row(), 0);
                leader.map_select().handle_mouse_motion(r0.x - 5, r0.y - 5);
                ASSERT_EQ(leader.map_select().hovered_row(), -1);
                // the guest's screen has the same rows and none of them can be pressed
                const std::pair<int32_t, int32_t> g = row_centre(guest, 1);
                ASSERT_EQ(guest.map_select().player_row_at(g.first, g.second), -1);
            }

            // presses that move nobody: the right button, a release on another row, a row of nobody, a guest's rows (it asks nothing: a guest's request would be counted as ignored)
            {
                const std::pair<int32_t, int32_t> gus_row = row_centre(leader, 1);
                leader.map_select().handle_mouse_motion(gus_row.first, gus_row.second);
                leader.map_select().handle_mouse_down(gus_row.first, gus_row.second, 3);
                ASSERT_EQ(leader.map_select().pressed_row(), -1);
                leader.map_select().handle_mouse_up(gus_row.first, gus_row.second, 3);
                leader.map_select().handle_mouse_down(gus_row.first, gus_row.second, 1);
                ASSERT_EQ(leader.map_select().pressed_row(), 1);                                            // (the press holds the row lit)
                const std::pair<int32_t, int32_t> ann_row = row_centre(leader, 0);
                leader.map_select().handle_mouse_motion(ann_row.first, ann_row.second);
                leader.map_select().handle_mouse_up(ann_row.first, ann_row.second, 1);                      // (let go on Ann's row)
                ASSERT_EQ(leader.map_select().pressed_row(), -1);
                press_row(leader, 2);
                press_row(leader, 3);
                ASSERT_EQ(leader.map_select().pressed_row(), -1);
                press_row(guest, 0);
                press_row(guest, 1);
                ASSERT_TRUE(guest.map_select().pressed_row() == -1 && guest.map_select().hovered_row() == -1);
                ASSERT_FALSE(guest.net()->request_move_seat(2));                                             // (a guest sends nothing, whoever it asks for)
                hall.step(400);
                const server::RoomStatus s = server.status(code);
                ASSERT_TRUE(s.names[0] == "Ann" && s.names[1] == "Gus" && s.seat_moves == 0 && s.ignored_seat_moves == 0);
                ASSERT_TRUE(leader.fill_bots() == plan(L::None, L::None, L::Hard, L::None));
            }

            // a press on Gus's row: the click of the buttons, and with the release Gus is put in the next free colour, Blue
            const std::vector<std::string> guest_notices = notices_of(guest);
            {
                const size_t channels = leader.audio_mixer().active_channel_count();
                const std::pair<int32_t, int32_t> at = row_centre(leader, 1);
                leader.map_select().handle_mouse_motion(at.first, at.second);
                leader.map_select().handle_mouse_down(at.first, at.second, 1);
                ASSERT_EQ(leader.audio_mixer().active_channel_count(), channels + 1);
                ASSERT_EQ(leader.map_select().pressed_row(), 1);
                leader.map_select().handle_mouse_up(at.first, at.second, 1);
                ASSERT_EQ(leader.map_select().pressed_row(), -1);
            }
            ASSERT_TRUE(hall.until([&]() { return leader.net()->room().slots[2].state == net::SlotState::Client && guest.net()->my_seat() == 2; }, 8000));
            hall.step(30);
            ASSERT_TRUE(leader.net()->room().slots[2].name == "Gus" && leader.net()->room().slots[1].state == net::SlotState::Empty && leader.net()->room().slots[0].name == "Ann");
            ASSERT_TRUE(leader.net()->my_seat() == 0 && leader.net()->is_leader() && guest.net()->room().leader == 0);
            ASSERT_TRUE(MapSelectScreen::row_seats(leader.map_select().room()) == Rows({0, 2, -1, -1}));         // (Gus's row is the second row still)
            ASSERT_TRUE(MapSelectScreen::row_seats(guest.map_select().room()) == Rows({2, 0, -1, -1}));
            ASSERT_TRUE(leader.fill_bots() == plan(L::None, L::Hard, L::None, L::None) && leader.net()->fill_bots() == leader.fill_bots());      // the bot that was for Blue is for Red now: START seats the same bots as before
            ASSERT_TRUE(leader.net()->prompt_texts().front().find("Red gets a Hard bot") != std::string::npos);
            ASSERT_TRUE(leader.map_select().room().status != hint);                                                // (the hint came once)
            {
                const std::vector<std::string> told = notices_of(guest);
                ASSERT_TRUE(told.size() == guest_notices.size() + 1 && told.back() == "Ann moved you to Blue.");   // Gus is told by the room
            }
            {
                const server::RoomStatus s = server.status(code);
                ASSERT_TRUE(s.names[0] == "Ann" && s.names[1].empty() && s.names[2] == "Gus" && s.seat_moves == 1 && s.ignored_seat_moves == 0 && s.leader == 0);
            }

            // two quick presses (a double click): the second is held back, and Gus goes on by one colour only (to Black)
            hall.step(500);                                                                                        // (the last request was half a second ago: its double click is over)
            press_row(leader, 1);
            press_row(leader, 1);
            ASSERT_TRUE(hall.until([&]() { return leader.net()->room().slots[3].state == net::SlotState::Client && guest.net()->my_seat() == 3; }, 8000));
            hall.step(500);
            ASSERT_TRUE(leader.net()->room().slots[2].state == net::SlotState::Empty && leader.net()->room().slots[3].name == "Gus");
            {
                const server::RoomStatus s = server.status(code);
                ASSERT_TRUE(s.seat_moves == 2 && s.ignored_seat_moves == 0);                                       // (a second request would have found Blue empty: ignored, and counted)
            }
            ASSERT_TRUE(leader.fill_bots() == plan(L::None, L::Hard, L::None, L::None));                           // (two colours that no bot is for: the plan stays)

            // Gus at Black goes on to the next free colour round the table: Green is Ann's, so Red; the bot of Red goes to the colour that Gus leaves
            press_row(leader, 1);
            ASSERT_TRUE(hall.until([&]() { return leader.net()->room().slots[1].state == net::SlotState::Client && guest.net()->my_seat() == 1; }, 8000));
            hall.step(30);
            ASSERT_TRUE(leader.net()->room().slots[1].name == "Gus" && leader.net()->room().slots[3].state == net::SlotState::Empty);
            ASSERT_TRUE(leader.fill_bots() == plan(L::None, L::None, L::None, L::Hard));
            ASSERT_TRUE(leader.net()->prompt_texts().front().find("Black gets a Hard bot") != std::string::npos);
            ASSERT_TRUE(server.status(code).seat_moves == 3 && server.status(code).ignored_seat_moves == 0);

            // the leader's own row moves the leader (to Blue: Red is Gus's); it is the leader still and the room does not tell it of its own move (the last request was half a second ago: a double click is over)
            const size_t leader_notices = notices_of(leader).size();
            hall.step(500);
            press_row(leader, 0);
            ASSERT_TRUE(hall.until([&]() { return leader.net()->my_seat() == 2 && guest.net()->room().leader == 2; }, 8000));
            hall.step(30);
            ASSERT_TRUE(leader.net()->is_leader() && leader.map_select().leads_server_room() && leader.map_select().room().my_seat == 2);
            ASSERT_TRUE(leader.net()->room().slots[0].state == net::SlotState::Empty && leader.net()->room().slots[2].name == "Ann" && leader.net()->room().slots[1].name == "Gus");
            ASSERT_TRUE(MapSelectScreen::row_seats(leader.map_select().room()) == Rows({2, 1, -1, -1}));          // (the leader's own seat first)
            ASSERT_EQ(notices_of(leader).size(), leader_notices);
            ASSERT_TRUE(server.status(code).seat_moves == 4 && server.status(code).leader == 2);
            ASSERT_TRUE(leader.fill_bots() == plan(L::None, L::None, L::None, L::Hard));

            // START: the match is made with the colours as they are now, Gus plays Red, Ann Blue, and the Hard bot Black
            {
                const server::RoomStatus before = server.status(code);
                ASSERT_TRUE(before.state == server::RoomState::Waiting && before.joined == 2 && before.ignored_start_requests == 0);
            }
            leader.map_select().handle_key_down(SDLK_RETURN);
            ASSERT_TRUE(hall.until([&]() { return leader.state() == AppState::Playing && guest.state() == AppState::Playing; }, 15000));
            {
                const server::RoomStatus s = server.status(code);
                ASSERT_TRUE(s.state == server::RoomState::Running && s.joined == 3 && s.bots.size() == 1 && s.expected == 4);
                ASSERT_TRUE(s.bots[0].seat == 3 && s.bots[0].level == "hard" && s.bots[0].name == "Bot (Hard)" && s.bots[0].fill);
                ASSERT_TRUE(s.names[0].empty() && s.names[1] == "Gus" && s.names[2] == "Ann" && s.names[3] == "Bot (Hard)");
            }
            ASSERT_TRUE(leader.local_player_id() == 2 && guest.local_player_id() == 1);
            ASSERT_TRUE(leader.sim().roster_mask() == 0x0E && guest.sim().roster_mask() == 0x0E);
            // the screen is locked now, and a press on a row asks nothing more of the room
            ASSERT_FALSE(leader.map_select().can_move_players());
            press_row(leader, 1);
            hall.step(kDialogMs + 1500);
            ASSERT_TRUE(hall.identical(leader.sim(), guest.sim()));
            ASSERT_FALSE(leader.net()->desynced() || guest.net()->desynced());
            {
                const server::RoomStatus s = server.status(code);
                ASSERT_TRUE(s.state == server::RoomState::Running && s.seat_moves == 4 && s.ignored_seat_moves == 0 && s.ticks > 20);
            }
            leader.quit();
            guest.quit();
        }
    } TEST_END();

    TEST_CASE("N5.92 Leader, The Colours (Protocol 14): A Double Click Is One Press (Nothing Goes Out Within Half A Second Of The Last Request, Whatever The Room Has Said); The Press After It Waits For The Room's Answer, Or A Second (A Request That The Room Cannot Do Is Not Answered At All); The Plan Of Bots Follows What The Room Shows And Not What Was Asked, However Often It Was Asked; A Guest Sends Nothing") {
        Server server;
        ASSERT_TRUE(server.make_room("MOVE-LATCH", 4));
        ApplicationConfig cfg = join_config(server, "MOVE-LATCH", "Ann");
        cfg.fill_bots = plan(L::None, L::None, L::Hard, L::None);                           // a Hard bot for Blue
        Application leader;
        ASSERT_TRUE(leader.init(cfg));
        Hall hall{server, &leader, {}};
        ASSERT_TRUE(hall.until([&]() { return leader.net()->phase() == net::NetGame::Phase::Room && leader.net()->is_leader(); }, 8000));
        Application guest;
        ASSERT_TRUE(guest.init(join_config(server, "MOVE-LATCH", "Gus")));
        hall.second = &guest;
        ASSERT_TRUE(hall.until([&]() { return guest.net()->phase() == net::NetGame::Phase::Room && leader.net()->room().slots[1].state == net::SlotState::Client; }, 8000));
        hall.step(100);
        ASSERT_EQ(guest.net()->my_seat(), 1);
        ASSERT_TRUE(guest.net()->room().leader == 0 && !guest.net()->is_leader());
        ASSERT_FALSE(guest.net()->request_move_seat(0));                                   // a guest sends nothing, whoever it asks for
        ASSERT_FALSE(guest.net()->request_move_seat(1));
        ASSERT_FALSE(leader.net()->request_move_seat(2));                                  // nobody sits in Blue
        ASSERT_FALSE(leader.net()->request_move_seat(255));
        hall.step(100);
        ASSERT_TRUE(server.status("MOVE-LATCH").seat_moves == 0 && server.status("MOVE-LATCH").ignored_seat_moves == 0);
        ASSERT_TRUE(leader.fill_bots() == plan(L::None, L::None, L::Hard, L::None));

        // the first press is sent (Gus to Blue, the next free colour); a second one, at once or less than half a second later, is a double click: held back, with or without an answer
        ASSERT_TRUE(leader.net()->request_move_seat(1));
        ASSERT_FALSE(leader.net()->request_move_seat(1));
        // only the leader's clock runs now, so no answer comes (a request that the room cannot do is not answered either): at 500 ms the double click is over, and the press waits for the answer or for a second
        for (int i = 0; i < 4; ++i) leader.pump_network(0.1f);                              // 400 ms
        ASSERT_FALSE(leader.net()->request_move_seat(1));
        leader.pump_network(0.1f);                                                          // 500 ms
        ASSERT_FALSE(leader.net()->request_move_seat(1));
        for (int i = 0; i < 4; ++i) leader.pump_network(0.1f);                              // 900 ms
        ASSERT_FALSE(leader.net()->request_move_seat(1));
        for (int i = 0; i < 2; ++i) leader.pump_network(0.1f);                              // 1100 ms
        ASSERT_TRUE(leader.net()->request_move_seat(1));                                    // (the same request again: the room still seats its people as it did)
        ASSERT_FALSE(leader.net()->request_move_seat(1));                                   // ... and that one is a double click again
        ASSERT_TRUE(leader.fill_bots() == plan(L::None, L::None, L::Hard, L::None));        // (nothing was shown yet: the plan is where it was, though two requests went out)

        // the room runs: it moves Gus once and finds Red empty for the second request, which it ignores and counts, no offence. The leader's clock stands still and it reads the news with no time passing
        hall.app = nullptr;
        ASSERT_TRUE(hall.until([&]() { const server::RoomStatus s = server.status("MOVE-LATCH"); return s.seat_moves == 1 && s.ignored_seat_moves == 1; }, 8000));
        ASSERT_TRUE(hall.until([&]() { return guest.net()->my_seat() == 2; }, 8000));
        ASSERT_TRUE(leader.net()->room().slots[1].state == net::SlotState::Client);          // (the leader has not read it yet)
        const auto read_leader = [&](const std::function<bool()>& cond) {
            for (int i = 0; i < 2000 && !cond(); ++i) {
                leader.pump_network(0.0f);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return cond();
        };
        ASSERT_TRUE(read_leader([&]() { return leader.net()->room().slots[2].state == net::SlotState::Client; }));
        ASSERT_TRUE(leader.net()->room().slots[2].name == "Gus" && leader.net()->room().slots[1].state == net::SlotState::Empty);
        ASSERT_TRUE(leader.fill_bots() == plan(L::None, L::Hard, L::None, L::None) && leader.net()->fill_bots() == leader.fill_bots());      // the plan followed the room, once: the bot that was for Blue is for Red
        {
            const server::RoomStatus s = server.status("MOVE-LATCH");
            ASSERT_TRUE(s.seat_moves == 1 && s.ignored_seat_moves == 1 && s.names[2] == "Gus" && s.leader == 0);
            ASSERT_TRUE(leader.net()->phase() == net::NetGame::Phase::Room && guest.net()->phase() == net::NetGame::Phase::Room);
        }

        // the room has answered, but no time has passed since the last request: a double click, still. Half a second after it, with the room's answer shown, the press goes out at once (Gus goes on to Black)
        ASSERT_FALSE(leader.net()->request_move_seat(2));
        leader.pump_network(0.4f);
        ASSERT_FALSE(leader.net()->request_move_seat(2));
        leader.pump_network(0.1f);
        ASSERT_TRUE(leader.net()->request_move_seat(2));
        hall.app = &leader;
        ASSERT_TRUE(hall.until([&]() { return leader.net()->room().slots[3].name == "Gus" && guest.net()->my_seat() == 3; }, 8000));
        ASSERT_TRUE(leader.net()->room().slots[2].state == net::SlotState::Empty);
        ASSERT_TRUE(server.status("MOVE-LATCH").seat_moves == 2 && server.status("MOVE-LATCH").ignored_seat_moves == 1);
        ASSERT_TRUE(leader.fill_bots() == plan(L::None, L::Hard, L::None, L::None));         // (two colours that no bot is for: the plan stays)

        // a request that the room ignores changes nothing in the plan: Cat takes Red while the leader has not read the news, so its press for Gus (Black goes to Red) finds Red held
        leader.pump_network(0.6f);                                                           // (more than half a second since the last request)
        Peer cat;
        hall.peers.push_back(&cat);
        hall.app = nullptr;
        ASSERT_TRUE(cat.net.join("127.0.0.1", server.port(), "Cat", 1, "MOVE-LATCH"));
        ASSERT_TRUE(hall.until([&]() { return cat.net.phase() == net::NetGame::Phase::Room && cat.net.my_seat() == 1; }, 8000));
        ASSERT_TRUE(leader.net()->room().slots[1].state == net::SlotState::Empty);           // (the leader has not read it)
        ASSERT_TRUE(leader.net()->request_move_seat(3));
        ASSERT_TRUE(hall.until([&]() { return server.status("MOVE-LATCH").ignored_seat_moves == 2; }, 8000));
        ASSERT_TRUE(read_leader([&]() { return leader.net()->room().slots[1].name == "Cat"; }));
        ASSERT_TRUE(leader.net()->room().slots[3].name == "Gus" && server.status("MOVE-LATCH").seat_moves == 2);
        ASSERT_TRUE(leader.fill_bots() == plan(L::None, L::Hard, L::None, L::None));         // Cat sat down and nobody moved: the plan stays
        hall.app = &leader;
        leader.quit();
        guest.quit();
    } TEST_END();

    TEST_CASE("N5.93 Leader, The Colours (Protocol 14): A Press Holds The Player Of The Row, Not The Place: When The Rows Close Up Before The Release (A Player Left) The Release Moves Nobody, And The Same Press On The Row That The Player Has Moved To Does") {
        Server server;
        ASSERT_TRUE(server.make_room("MOVE-HOLD", 4));
        Application leader;
        ASSERT_TRUE(leader.init(join_config(server, "MOVE-HOLD", "Ann")));
        Peer gus;
        Peer cat;
        Hall hall{server, &leader, {&gus, &cat}};
        ASSERT_TRUE(hall.until([&]() { return leader.net()->phase() == net::NetGame::Phase::Room && leader.net()->is_leader(); }, 8000));
        ASSERT_TRUE(gus.net.join("127.0.0.1", server.port(), "Gus", 255, "MOVE-HOLD"));
        ASSERT_TRUE(hall.until([&]() { return gus.net.phase() == net::NetGame::Phase::Room && gus.net.my_seat() == 1; }, 8000));
        ASSERT_TRUE(cat.net.join("127.0.0.1", server.port(), "Cat", 255, "MOVE-HOLD"));
        ASSERT_TRUE(hall.until([&]() { return cat.net.phase() == net::NetGame::Phase::Room && cat.net.my_seat() == 2 && leader.net()->room().slots[2].state == net::SlotState::Client; }, 8000));
        settle(leader);
        hall.step(50);
        ASSERT_TRUE(MapSelectScreen::row_seats(leader.map_select().room()) == Rows({0, 1, 2, -1}));
        // the press is on Gus's row (the second); Gus leaves, the rows close up and Cat's row is the second; the release is where the press was
        const std::pair<int32_t, int32_t> at = row_centre(leader, 1);
        leader.map_select().handle_mouse_motion(at.first, at.second);
        leader.map_select().handle_mouse_down(at.first, at.second, 1);
        ASSERT_EQ(leader.map_select().pressed_row(), 1);
        gus.net.leave();
        ASSERT_TRUE(hall.until([&]() { return leader.net()->room().slots[1].state == net::SlotState::Empty; }, 8000));
        hall.step(50);
        ASSERT_TRUE(MapSelectScreen::row_seats(leader.map_select().room()) == Rows({0, 2, -1, -1}));
        leader.map_select().handle_mouse_up(at.first, at.second, 1);
        ASSERT_EQ(leader.map_select().pressed_row(), -1);
        hall.step(500);
        ASSERT_TRUE(server.status("MOVE-HOLD").seat_moves == 0 && server.status("MOVE-HOLD").ignored_seat_moves == 0);
        ASSERT_TRUE(leader.net()->room().slots[2].name == "Cat" && leader.net()->room().slots[3].state == net::SlotState::Empty);        // Cat sits where she sat
        // the whole press on Cat's row moves Cat (to Black: the next free colour after Blue)
        press_row(leader, 1);
        ASSERT_TRUE(hall.until([&]() { return leader.net()->room().slots[3].name == "Cat"; }, 8000));
        ASSERT_TRUE(server.status("MOVE-HOLD").seat_moves == 1 && server.status("MOVE-HOLD").ignored_seat_moves == 0);
        leader.quit();
    } TEST_END();

    TEST_CASE("N5.94 Leader, The Colours (Protocol 14): The Hint Is For The Waiting Room (A START That Is Pressed While It Shows Leaves The Loading Screen With Its Own Words); A Net Game That Leaves A Room And Joins Another One Starts Afresh: The Hint Again, And No Wait For The Last Press Of The Room That Is Gone") {
        const std::string hint = "Tap a player to change their colour.";
        Server server;
        ASSERT_TRUE(server.make_room("MOVE-HINT-A", 4));
        ASSERT_TRUE(server.make_room("MOVE-HINT-B", 4));
        Peer lea;
        Peer bob;
        Peer dan;
        dan.hold_report = true;                                                              // (Dan's machine is slow to report the map: the second room stays in its loading phase)
        Hall hall{server, nullptr, {&lea, &bob, &dan}};
        ASSERT_TRUE(lea.net.join("127.0.0.1", server.port(), "Lea", 255, "MOVE-HINT-A"));
        ASSERT_TRUE(hall.until([&]() { return lea.net.phase() == net::NetGame::Phase::Room && lea.net.is_leader(); }, 8000));
        ASSERT_TRUE(lea.net.status_text() != hint);                                          // (alone: nobody to move)
        ASSERT_TRUE(bob.net.join("127.0.0.1", server.port(), "Bob", 255, "MOVE-HINT-A"));
        ASSERT_TRUE(hall.until([&]() { return bob.net.phase() == net::NetGame::Phase::Room && lea.net.room().slots[1].state == net::SlotState::Client; }, 8000));
        ASSERT_EQ(lea.net.status_text(), hint);                                              // the leader's first company
        hall.step(5200);                                                                     // (the hint is shown for five seconds)
        ASSERT_TRUE(lea.net.status_text() != hint);
        ASSERT_TRUE(lea.net.request_move_seat(1));                                           // Bob goes to Blue: the press is the last of this room
        ASSERT_TRUE(hall.until([&]() { return lea.net.room().slots[2].state == net::SlotState::Client; }, 8000));
        lea.net.leave();
        // another room, a fresh session: the second person brings the hint again, and the first press is not held for the press that was made in the room that is gone
        ASSERT_TRUE(lea.net.join("127.0.0.1", server.port(), "Lea", 255, "MOVE-HINT-B"));
        ASSERT_TRUE(hall.until([&]() { return lea.net.phase() == net::NetGame::Phase::Room && lea.net.is_leader(); }, 8000));
        ASSERT_TRUE(dan.net.join("127.0.0.1", server.port(), "Dan", 255, "MOVE-HINT-B"));
        ASSERT_TRUE(hall.until([&]() { return dan.net.phase() == net::NetGame::Phase::Room && lea.net.room().slots[1].state == net::SlotState::Client; }, 8000));
        ASSERT_EQ(lea.net.status_text(), hint);
        ASSERT_TRUE(lea.net.request_move_seat(1));
        ASSERT_TRUE(hall.until([&]() { return lea.net.room().slots[2].state == net::SlotState::Client; }, 8000));
        // START while the hint shows: the room loads, and the status line says so (the screen is locked: nothing is left to tap)
        ASSERT_EQ(lea.net.status_text(), hint);
        ASSERT_TRUE(lea.net.request_start());
        ASSERT_TRUE(hall.until([&]() { return lea.net.phase() == net::NetGame::Phase::Loading && dan.report_pending; }, 10000));
        ASSERT_TRUE(lea.net.status_text() != hint);
        ASSERT_TRUE(lea.net.status_text() == std::string(sim::strings::text(sim::strings::kLoadingGame)) || lea.net.status_text() == std::string(sim::strings::text(sim::strings::kWaitingForOthers)));
    } TEST_END();

    TEST_CASE("N5.75 The \"Get Ready\" Dialog In A Server's Room (Protocol 12): A Leader Who Starts A Room With Bots Sees The Dialog For The 5 s Before The Referee's First Turn, Up Exactly While No Tick Has Run And Gone The Step The First One Ran; The Referee Seals Nothing And Runs Nothing Meanwhile, The Screen Says Nothing, The Bots Look And Send Nothing; A Click Selects Nothing Until It Is Gone") {
        Server server;
        ASSERT_TRUE(server.make_room("DIALOG-APP", 4));
        ApplicationConfig cfg = join_config(server, "DIALOG-APP", "Solo");
        cfg.fill_bots = net::FillLevel::Hard;
        Application app;
        ASSERT_TRUE(app.init(cfg));
        Hall hall{server, &app, {}};
        ASSERT_TRUE(hall.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && app.net()->is_leader(); }, 8000));
        click_start(app);
        ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing; }, 15000));
        ASSERT_TRUE(app.hud().is_match_start_modal_active() && app.sim().current_tick() == 0);
        ASSERT_TRUE(server.status("DIALOG-APP").state == server::RoomState::Running && server.status("DIALOG-APP").bots.size() == 3);
        const uint32_t full_time = app.sim().get_match_time_remaining_ms();
        const uint32_t mine = static_cast<uint32_t>(ants_of(app.sim(), 0).front());
        uint32_t waited = 0;
        while (app.sim().current_tick() == 0 && waited < 20000) {
            ASSERT_TRUE(app.hud().is_match_start_modal_active());                              // up exactly while no tick has run
            ASSERT_EQ(app.sim().get_match_time_remaining_ms(), full_time);
            const server::RoomStatus st = server.status("DIALOG-APP");
            if (st.turns == 0) ASSERT_TRUE(st.ticks == 0 && !st.paused);                         // the referee seals nothing and runs nothing before its first turn
            ASSERT_TRUE(app.net_overlay_now().text.empty() && app.net()->stalled_ms() == 0 && !app.net()->lag_notice() && !app.net()->catching_up() && !app.net()->self_lag_behind_ms());
            if (waited == 2000) {
                for (const auto& a : app.sim().get_world_state().ants) {
                    if (a.id != mine) continue;
                    app.renderer().camera().center_on(a.px, a.py, app.sim().grid().width(), app.sim().grid().height());
                    int32_t sx = 0;
                    int32_t sy = 0;
                    ASSERT_TRUE(app.renderer().camera().world_to_screen(a.px, a.py, sx, sy));
                    ASSERT_TRUE(app.hud().handle_mouse_down(sx, sy, 1, app.sim(), app.renderer().camera()));
                    app.hud().handle_mouse_up(sx, sy, 1, app.sim(), app.renderer().camera());
                    ASSERT_TRUE(app.hud().get_selected_ant_ids().empty() && app.hud().get_selected_ant_id() == 0u);
                }
            }
            hall.step(10);
            waited += 10;
        }
        ASSERT_TRUE(app.sim().current_tick() >= 1);
        ASSERT_FALSE(app.hud().is_match_start_modal_active());                                  // gone on the step that the first turn ran
        ASSERT_TRUE(waited >= kDialogMs - 100 && waited <= kDialogMs + 400);                    // 5 s (the Begin was read a step or two before the check above) and the link, the second turn, the frames
        ASSERT_TRUE(server.status("DIALOG-APP").bot_start_hold == ai::kStartHoldTicks);         // the room's bots open like the product's: one token, a look on tick 1 + seat
        hall.step(3000);
        ASSERT_TRUE(server.status("DIALOG-APP").ticks > 40 && !app.net()->desynced() && app.net_overlay_now().text.empty());
        app.quit();
    } TEST_END();
}

void run_window_tests() {
    TEST_CASE("N5.15 Window: --window-size and --window-pos put the window where they say") {
        ApplicationConfig cfg = headless_config();
        cfg.has_window_size = true;
        cfg.window_w = 800;
        cfg.window_h = 600;
        cfg.has_window_pos = true;
        cfg.window_x = 30;
        cfg.window_y = 40;
        Application app;
        ASSERT_TRUE(app.init(cfg));
        const WindowRect r = app.window_rect();
        ASSERT_TRUE(r.w == 800 && r.h == 600 && r.x == 30 && r.y == 40);
    } TEST_END();

    TEST_CASE("N5.16 Window: --grid 2x2 gives four windows that lie inside the display and do not overlap, in reading order") {
        WindowRect cells[4];
        SDL_Rect area{0, 0, 0, 0};
        for (int i = 0; i < 4; ++i) {
            ApplicationConfig cfg = headless_config();
            cfg.grid_cols = 2;
            cfg.grid_rows = 2;
            cfg.grid_cell = i;
            Application app;
            ASSERT_TRUE(app.init(cfg));
            cells[i] = app.window_rect();
            ASSERT_TRUE(cells[i].w >= kMinWindowWidth && cells[i].h >= kMinWindowHeight);
            ASSERT_TRUE(std::abs(cells[i].w * 3 - cells[i].h * 4) <= 4);              // the game's own 4:3 (to a pixel of rounding)
            if (i == 0) ASSERT_EQ(SDL_GetDisplayUsableBounds(0, &area), 0);
            ASSERT_TRUE(cells[i].x >= area.x && cells[i].y >= area.y);
            ASSERT_TRUE(cells[i].x + cells[i].w <= area.x + area.w && cells[i].y + cells[i].h <= area.y + area.h);
        }
        ASSERT_TRUE(cells[0].x + cells[0].w <= cells[1].x && cells[2].x + cells[2].w <= cells[3].x);       // left to right
        ASSERT_TRUE(cells[0].y + cells[0].h <= cells[2].y && cells[1].y + cells[1].h <= cells[3].y);       // top to bottom
        ASSERT_TRUE(cells[0].x == cells[2].x && cells[1].x == cells[3].x && cells[0].y == cells[1].y && cells[2].y == cells[3].y);
    } TEST_END();

    TEST_CASE("N5.17 Pointer: leaving the window stops the edge scroll and hides the game's cursor; any pointer event in the window ends it") {
        ApplicationConfig cfg = headless_config();
        Application app;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_TRUE(app.start_game("Original-Ants/Maps/SMALL.LVL"));
        app.hud().dismiss_match_start_modal();                                           // (the match start notice takes the mouse until it is gone)
        SDL_WindowEvent leave{};
        leave.type = SDL_WINDOWEVENT;
        leave.event = SDL_WINDOWEVENT_LEAVE;
        SDL_WindowEvent enter = leave;
        enter.event = SDL_WINDOWEVENT_ENTER;
        SDL_MouseMotionEvent motion{};
        motion.type = SDL_MOUSEMOTION;
        motion.x = 0;                                                                    // the left scroll strip
        motion.y = 240;
        auto scrolled = [&]() {
            app.renderer().camera().world_x = 400;
            app.renderer().camera().world_y = 400;
            for (int i = 0; i < 6; ++i) app.handle_camera_panning(0.05f);
            return app.renderer().camera().world_x != 400 || app.renderer().camera().world_y != 400;
        };
        ASSERT_FALSE(app.pointer_outside());
        app.handle_mouse_motion(motion);
        ASSERT_TRUE(scrolled());                                                         // the pointer rests on the edge: the view scrolls
        app.handle_window_event(leave);
        ASSERT_TRUE(app.pointer_outside());
        ASSERT_FALSE(scrolled());                                                        // the window has been left: the last position must not keep scrolling
        app.handle_window_event(enter);
        ASSERT_FALSE(app.pointer_outside());
        ASSERT_TRUE(scrolled());
        app.handle_window_event(leave);
        ASSERT_TRUE(app.pointer_outside());
        motion.x = 320;
        motion.y = 240;
        app.handle_mouse_motion(motion);                                                 // a motion event can only come from a pointer inside the window
        ASSERT_FALSE(app.pointer_outside());
        app.handle_window_event(leave);
        SDL_MouseButtonEvent press{};
        press.type = SDL_MOUSEBUTTONDOWN;
        press.button = SDL_BUTTON_LEFT;
        press.x = 320;
        press.y = 240;
        app.handle_mouse_button(press);                                                  // ... and so can a button event
        ASSERT_FALSE(app.pointer_outside());
    } TEST_END();

    TEST_CASE("N5.18 Sound follows the focus with --audio-focus only") {
        for (int follow = 0; follow < 2; ++follow) {
            ApplicationConfig cfg = headless_config();
            cfg.audio_follows_focus = follow != 0;
            Application app;
            ASSERT_TRUE(app.init(cfg));
            ASSERT_TRUE(app.start_game("Original-Ants/Maps/SMALL.LVL"));
            const int volume = app.audio_mixer().sound_volume();
            ASSERT_TRUE(volume > 0);
            SDL_WindowEvent lost{};
            lost.type = SDL_WINDOWEVENT;
            lost.event = SDL_WINDOWEVENT_FOCUS_LOST;
            app.handle_window_event(lost);
            ASSERT_EQ(app.audio_mixer().sound_volume(), follow != 0 ? 0 : volume);            // a window without the focus is silent ...
            SDL_WindowEvent gained = lost;
            gained.event = SDL_WINDOWEVENT_FOCUS_GAINED;
            app.handle_window_event(gained);
            ASSERT_EQ(app.audio_mixer().sound_volume(), volume);                               // ... and the sound is back with the focus
        }
    } TEST_END();
    TEST_CASE("N5.19 Guest: --seat N asks the host for that colour (the start script seats window i in colour i)") {
        Peer host;
        ASSERT_TRUE(host.net.host(0, "Alice", true));
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Join;
        cfg.net_address = "127.0.0.1";
        cfg.net_port = host.net.listen_port();
        cfg.player_name = "Dave";
        cfg.net_seat = 3;                                                                // black, although seats 1 and 2 are free
        Application app;
        ASSERT_TRUE(app.init(cfg));
        Duo duo{app, host};
        ASSERT_TRUE(duo.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && app.net()->my_seat() == 3; }, 8000));
        ASSERT_EQ(host.net.room().slots[3].name, "Dave");
    } TEST_END();
}


// ping and delay next to the frame rate (latency_corner.hpp, ants_net/latency.hpp): what a guest and a host measure in the room and in the match, and what the corner does
void run_latency_tests() {
    TEST_CASE("N5.27 Latency: A Guest Measures Its Ping In The Room And Its Ping And The Delay Of Its Own Commands In The Match; The Corner Draws Them; Nothing Waits At The End") {
        Peer host;
        ASSERT_TRUE(host.net.host(0, "Alice", true));
        host.net.set_map("TINY.LVL");
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Join;
        cfg.net_address = "127.0.0.1";
        cfg.net_port = host.net.listen_port();
        cfg.player_name = "Bob";
        Application app;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_FALSE(app.net()->ping_ms().has_value());                                   // nothing is measured before the guest has a seat
        ASSERT_FALSE(app.net()->command_delay_ms().has_value());
        Duo duo{app, host};
        ASSERT_TRUE(duo.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && host.net.can_start(); }, 8000));
        // the room: the guest pings the host once it has a seat (the game's clock is virtual and steps 10 ms: the answer is read one step later)
        ASSERT_TRUE(duo.until([&]() { return app.net()->ping_ms().has_value(); }, 8000));
        ASSERT_TRUE(*app.net()->ping_ms() <= 30);
        ASSERT_FALSE(app.net()->command_delay_ms().has_value());                           // no command yet: "-"
        app.render_frame();                                                                // the room's corner: one row left of the version
        // the match
        uint64_t hash = 0;
        ASSERT_TRUE(net::hash_file(maps_dir() + "TINY.LVL", hash));
        ASSERT_TRUE(host.net.start_match(777, hash));
        ASSERT_TRUE(duo.until([&]() { return app.state() == AppState::Playing && host.net.phase() == net::NetGame::Phase::Playing; }, 8000));
        ASSERT_TRUE(app.net()->ping_ms().has_value());                                     // the room's measurement carries over to the first moments of the match
        ASSERT_FALSE(app.net()->command_delay_ms().has_value());
        uint32_t next = 0;
        for (uint32_t t = 0; t < 12000; t += 10) {
            if (t >= next) {
                next = t + 700;
                const auto mine = ants_of(app.sim(), 1);
                if (!mine.empty()) app.net()->submit(order(1, mine[(t / 700) % mine.size()], static_cast<int16_t>((t / 10) % 31), static_cast<int16_t>((t / 20) % 31)));
            }
            duo.step(10);
        }
        ASSERT_TRUE(app.net()->ping_ms().has_value() && *app.net()->ping_ms() <= 30);
        ASSERT_TRUE(app.net()->command_delay_ms().has_value());
        // the delay: the way there (up to one step), the wait for the next 50 ms turn (0 - 50), the way back (up to one step), and one turn of jitter buffer (50 - 60)
        ASSERT_TRUE(*app.net()->command_delay_ms() >= 50 && *app.net()->command_delay_ms() <= 150);
        ASSERT_TRUE(app.net()->stalled_ms() < 1000);                                       // turns are flowing: no "Waiting for the other players..."
        app.render_frame();                                                                // the match's corner: two lines above the row
        // a player who gives no order for more than ten seconds sees "delay -" (the number is not about now any more), and the next order brings it back
        duo.step(9000);
        ASSERT_TRUE(app.net()->command_delay_ms().has_value());
        duo.step(2500);
        ASSERT_FALSE(app.net()->command_delay_ms().has_value());
        app.render_frame();                                                                // (the corner draws the dash)
        for (uint32_t t = 0; t < 2000 && !app.net()->command_delay_ms().has_value(); t += 10) {
            if (t % 300 == 0) {
                const auto mine = ants_of(app.sim(), 1);
                if (!mine.empty()) app.net()->submit(order(1, mine[(t / 300) % mine.size()], static_cast<int16_t>(5 + t % 20), 7));
            }
            duo.step(10);
        }
        ASSERT_TRUE(app.net()->command_delay_ms().has_value());
        // the results screen of a match shows the readout too (one row: nothing stands in the corner row there)
        app.return_to_map_select();
        ASSERT_FALSE(app.network_active());
        ASSERT_FALSE(app.net() != nullptr && app.net()->ping_ms().has_value());
    } TEST_END();

    TEST_CASE("N5.28 Latency: A Host's Ping Is 0 (it has no link to itself), The Delay Of Its Own Commands Is The Wait For The Seal Plus One Turn Of Buffer") {
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Host;
        cfg.net_port = 0;
        cfg.net_loopback_only = true;
        cfg.player_name = "Alice";
        Application app;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_TRUE(app.net()->ping_ms().has_value() && *app.net()->ping_ms() == 0);        // in the room, alone
        ASSERT_FALSE(app.net()->command_delay_ms().has_value());
        int32_t tiny = -1;
        for (size_t i = 0; i < app.map_select().get_maps().size(); ++i) {
            if (app.map_select().get_maps()[i].filename == "TINY.LVL") tiny = static_cast<int32_t>(i);
        }
        ASSERT_TRUE(tiny >= 0);
        app.map_select().set_selected_index(tiny);
        Peer bob;
        ASSERT_TRUE(bob.net.join("127.0.0.1", app.net()->listen_port(), "Bob"));
        Duo duo{app, bob};
        ASSERT_TRUE(duo.until([&]() { return app.net()->can_start(); }, 8000));
        app.map_select().handle_key_down(SDLK_RETURN);
        ASSERT_TRUE(duo.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 8000));
        ASSERT_TRUE(app.net()->ping_ms().has_value() && *app.net()->ping_ms() == 0);        // in the match
        uint32_t next = 0;
        for (uint32_t t = 0; t < 12000; t += 10) {
            if (t >= next) {
                next = t + 700;
                const auto mine = ants_of(app.sim(), 0);
                if (!mine.empty()) app.net()->submit(order(0, mine[(t / 700) % mine.size()], static_cast<int16_t>((t / 10) % 31), static_cast<int16_t>((t / 20) % 31)));
            }
            duo.step(10);
        }
        ASSERT_TRUE(app.net()->command_delay_ms().has_value());
        ASSERT_TRUE(*app.net()->command_delay_ms() >= 50 && *app.net()->command_delay_ms() <= 120);        // 0 - 50 for the seal, 50 for the buffer, one step of rounding
        app.render_frame();                                                                  // the host's corner: "ping 0 ms", its own delay
    } TEST_END();

    TEST_CASE("N5.30 The Network Keeps Real Time: A Frame Of 400 ms Lets The Runner Pay Back Eight Turns At Once (the local simulation's clamp of 100 ms would run two and leave the rest in the queue)") {
        Peer host;
        ASSERT_TRUE(host.net.host(0, "Alice", true));
        host.net.set_map("TINY.LVL");
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Join;
        cfg.net_address = "127.0.0.1";
        cfg.net_port = host.net.listen_port();
        cfg.player_name = "Bob";
        Application app;
        ASSERT_TRUE(app.init(cfg));
        Duo duo{app, host};
        ASSERT_TRUE(duo.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && host.net.can_start(); }, 8000));
        uint64_t hash = 0;
        ASSERT_TRUE(net::hash_file(maps_dir() + "TINY.LVL", hash));
        ASSERT_TRUE(host.net.start_match(4242, hash));
        ASSERT_TRUE(duo.until([&]() { return app.state() == AppState::Playing && host.net.phase() == net::NetGame::Phase::Playing; }, 8000));
        duo.step(kDialogMs + 1000);                                                       // the first turn is sealed kDialogMs after the match began; then the match runs: one turn of buffer
        ASSERT_TRUE(app.net()->stalled_ms() < 1000);
        ASSERT_FALSE(app.hud().is_match_start_modal_active());                            // (the dialog ended with the first turn)
        // the window is busy for 400 ms (a hitch): the host goes on sealing, its turns wait in the socket
        for (int i = 0; i < 40; ++i) {
            host.now += 10;
            host.update();
            std::this_thread::sleep_for(std::chrono::microseconds(300));
        }
        const uint32_t before = app.net()->turns_executed();
        app.run_frame_with_delta(0.4f);                                                   // the frame that follows the hitch: 400 ms of real time
        const uint32_t paid_back = app.net()->turns_executed() - before;
        ASSERT_TRUE(paid_back >= 6);                                                      // 400 ms of ticks at once: eight turns of 50 ms (a clamp to 100 ms would run two)
        duo.step(1000);
        ASSERT_TRUE(app.net()->stalled_ms() < 1000);
    } TEST_END();

    TEST_CASE("N5.31 A Ping Reading Older Than Three Seconds Is Not Shown: The Host Stops Answering For 3.5 s (The Corner Says \"ping -\"), Then Answers Again (A Number Again)") {
        Peer host;
        ASSERT_TRUE(host.net.host(0, "Alice", true));
        host.net.set_map("TINY.LVL");
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Join;
        cfg.net_address = "127.0.0.1";
        cfg.net_port = host.net.listen_port();
        cfg.player_name = "Bob";
        Application app;
        ASSERT_TRUE(app.init(cfg));
        Duo duo{app, host};
        ASSERT_TRUE(duo.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && host.net.can_start(); }, 8000));
        uint64_t hash = 0;
        ASSERT_TRUE(net::hash_file(maps_dir() + "TINY.LVL", hash));
        ASSERT_TRUE(host.net.start_match(4343, hash));
        ASSERT_TRUE(duo.until([&]() { return app.state() == AppState::Playing && host.net.phase() == net::NetGame::Phase::Playing; }, 8000));
        duo.step(3000);
        ASSERT_TRUE(app.net()->ping_ms().has_value() && *app.net()->ping_ms() <= 30);
        // the host's machine stops (its process hangs: it reads nothing, answers nothing); the guest's clock goes on
        const auto guest_only = [&](uint32_t ms) {
            for (uint32_t t = 0; t < ms; t += 10) {
                app.pump_network(0.010f);
                app.update_simulation(0.010f);
                std::this_thread::sleep_for(std::chrono::microseconds(300));
            }
        };
        guest_only(1500);
        ASSERT_TRUE(app.net()->ping_ms().has_value());                                    // the last answer is 1.5 s old at the least and 2.5 s at the most (a ping goes out every second): still a reading
        guest_only(2200);                                                                 // 3.7 s after the host stopped: the last answer is older than 3 s whenever it came
        ASSERT_FALSE(app.net()->ping_ms().has_value());                                   // "ping -"
        app.render_frame();                                                               // (the corner draws the dash)
        duo.step(1500);                                                                   // the host is back: the answers that waited come (the pings it did not answer), the new ones too
        ASSERT_TRUE(app.net()->ping_ms().has_value());
    } TEST_END();

    TEST_CASE("N5.29 Latency Corner With The Real Font: The Row Of The Setup And Results Screens And The Two Lines Of The Match Clear The Version, The Frame Rate, The Sparkline And The Score Boxes") {
        ApplicationConfig cfg = headless_config();
        Application app;
        ASSERT_TRUE(app.init(cfg));
        Renderer& r = app.renderer();
        const int32_t text_h = r.get_text_height(FontSize::Px12);
        const int32_t fps_w = r.get_text_width("144 FPS", FontSize::Px12);                  // the widest frame rate text that matters (three digits)
        const int32_t text_x = 632 - fps_w;
        const int32_t spark_x = text_x - 36 - 6;
        const int32_t ver_w = r.get_text_width(std::string(ants::VERSION_STRING), FontSize::Px12);
        const int32_t ver_x = spark_x - ver_w - 6;
        const int32_t text_y = FPS_OVERLAY_SPARK_Y + (FPS_OVERLAY_SPARK_H - text_h) / 2;
        const int32_t widest = r.get_text_width(ping_text(LATENCY_SHOWN_MAX_MS), FontSize::Px12) + LATENCY_TEXT_GAP + r.get_text_width(delay_text(LATENCY_SHOWN_MAX_MS), FontSize::Px12);
        struct Box { int32_t x, y, w, h; };
        auto overlap = [](const Box& a, const Box& b) { return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h; };
        for (const uint32_t ms : {0u, 7u, 42u, 230u, 1234u, 9999u}) {
            const std::string p = ping_text(ms);
            const std::string d = delay_text(ms);
            const int32_t pw = r.get_text_width(p, FontSize::Px12);
            const int32_t dw = r.get_text_width(d, FontSize::Px12);
            const Box version{ver_x, text_y, ver_w, text_h};
            const Box plate{spark_x - 1, FPS_OVERLAY_TOP, 36 + 2, FPS_OVERLAY_BOTTOM - FPS_OVERLAY_TOP};
            const Box fps{text_x, text_y, fps_w, text_h};
            // setup and results screens: one row, left of the version; the prompt box of the setup screen (36 .. 329, picture to 336, rows 441 .. 468) is further left
            for (const int32_t limit : {LATENCY_LEFT_LIMIT_SETUP, LATENCY_LEFT_LIMIT_RESULTS}) {
                const LatencyCornerLayout l = layout_latency_corner(pw, dw, widest, text_h, ver_x, text_y, limit);
                ASSERT_FALSE(l.stacked);
                const Box ping{l.ping_x, l.ping_y, pw, text_h};
                const Box delay{l.delay_x, l.delay_y, dw, text_h};
                ASSERT_FALSE(overlap(ping, version) || overlap(delay, version) || overlap(ping, plate) || overlap(delay, plate) || overlap(ping, fps) || overlap(delay, fps) || overlap(ping, delay));
                ASSERT_TRUE(l.ping_x >= limit);
                ASSERT_TRUE(l.ping_y == text_y && l.delay_y == text_y);                      // level with the frame rate, in the corner row
            }
            ASSERT_TRUE(layout_latency_corner(pw, dw, widest, text_h, ver_x, text_y, LATENCY_LEFT_LIMIT_SETUP).ping_x > 336);                  // right of the prompt box's picture
            // match: two lines above the row. They clear the version, the sparkline plate, the frame rate and the score boxes (bottom row, to x = 458) and the chat input box
            // (x 479 .. 622, rows 423 .. 436); the only HUD piece they touch is the right edge of the "Send to: All" button (x 532 .. 576, rows 443 .. 467), by at most 7 pixels
            const LatencyCornerLayout m = layout_latency_corner(pw, dw, widest, text_h, ver_x, text_y, LATENCY_LEFT_LIMIT_MATCH);
            ASSERT_TRUE(m.stacked);
            const Box ping{m.ping_x, m.ping_y, pw, text_h};
            const Box delay{m.delay_x, m.delay_y, dw, text_h};
            ASSERT_FALSE(overlap(ping, version) || overlap(delay, version) || overlap(ping, plate) || overlap(delay, plate) || overlap(ping, fps) || overlap(delay, fps) || overlap(ping, delay));
            const Box score_boxes{0, 461, 458, 19};
            const Box chat_input{479, 423, 143, 14};
            ASSERT_FALSE(overlap(ping, score_boxes) || overlap(delay, score_boxes) || overlap(ping, chat_input) || overlap(delay, chat_input));
            // the "Send to: All" button's box is x 532 .. 575, rows 443 .. 466 (its picture ends a pixel or two inside it): the lines end at the frame rate's right edge, so a longer
            // text reaches further into the box: "delay 230 ms" 7 px, "ping 230 ms" 2, a number of one or two digits none, "delay 9999 ms" 13 (and "ping 9999 ms" 9)
            const Box all_button{532, 443, 44, 24};
            auto touches = [&](const Box& line) { return std::max(0, std::min(all_button.x + all_button.w, line.x + line.w) - std::max(all_button.x, line.x)); };
            ASSERT_TRUE(touches(delay) <= 13 && touches(ping) <= 9);
            ASSERT_TRUE(ms >= 1000 || (touches(delay) <= 7 && touches(ping) <= 2));
            ASSERT_TRUE(ms >= 100 || (touches(delay) == 0 && touches(ping) == 0));
            ASSERT_TRUE(m.ping_x >= 0 && m.delay_x >= 0 && m.delay_y + text_h <= 480 && m.ping_y >= 436);
        }
    } TEST_END();

    TEST_CASE("N5.29b Latency Corner In The 16:9 Match: The Application's Readout Stands On Two Lines In The Corner, Right Of The Row Of Score Boxes That Are Spread Over The Wide Bottom Strip (The Match's Limit Is The Layout's, Not The Original's 460), And In The Original's Own Picture Still On Two Lines Right Of 460") {
        for (const Aspect aspect : {Aspect::Wide16x9, Aspect::Classic4x3}) {
            Peer host;
            ASSERT_TRUE(host.net.host(0, "Alice", true));
            host.net.set_map("TINY.LVL");
            ApplicationConfig cfg = headless_config();
            cfg.aspect = aspect;
            cfg.aspect_given = true;
            cfg.net_role = ApplicationConfig::NetRole::Join;
            cfg.net_address = "127.0.0.1";
            cfg.net_port = host.net.listen_port();
            cfg.player_name = "Bob";
            Application app;
            ASSERT_TRUE(app.init(cfg));
            Duo duo{app, host};
            ASSERT_TRUE(duo.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && host.net.can_start(); }, 8000));
            app.render_frame();
            ASSERT_TRUE(app.last_latency_layout().has_value());                         // the room's corner: one row (a page; no match yet)
            uint64_t hash = 0;
            ASSERT_TRUE(net::hash_file(maps_dir() + "TINY.LVL", hash));
            ASSERT_TRUE(host.net.start_match(4242, hash));
            ASSERT_TRUE(duo.until([&]() { return app.state() == AppState::Playing && host.net.phase() == net::NetGame::Phase::Playing; }, 8000));
            duo.step(300);
            app.render_frame();
            ASSERT_TRUE(app.last_latency_layout().has_value());
            const LatencyCornerLayout l = *app.last_latency_layout();
            const ScreenLayout& layout = app.layout();
            ASSERT_EQ(layout.width, aspect == Aspect::Wide16x9 ? 960 : 640);
            const int32_t row_right = layout.score_row_right();                         // 780 at 960 x 540 (the third box 722 + 58), 460 in the original's picture
            ASSERT_EQ(row_right, aspect == Aspect::Wide16x9 ? 780 : 460);
            ASSERT_TRUE(l.stacked);                                                     // a match's corner row has room for no row of texts, in either picture
            ASSERT_TRUE(l.ping_x >= row_right && l.delay_x >= row_right);               // right of the last score box and its cover
            ASSERT_EQ(l.delay_y + app.renderer().get_text_height(FontSize::Px12) + 1, layout.height - (480 - FPS_OVERLAY_TOP));     // the lower line is a row above the plate
            ASSERT_TRUE(l.ping_y + app.renderer().get_text_height(FontSize::Px12) <= l.delay_y);
            // a frame on a page of the original's (the results) is a row left of the version, and a frame of a match with no network draws nothing: checked where the screens are made
            app.return_to_map_select();
            app.render_frame();
            ASSERT_FALSE(app.last_latency_layout().has_value());                        // the connection is closed with the match: nothing is drawn
        }
    } TEST_END();
}

// ------------------------------------------------------------------------------------------------------------------------------------------
// A hidden page (the web build): a hidden or minimised browser tab runs no frames and the browser slows its timers, but the page's WebSocket events still arrive. A network
// match is then driven by those wake-ups (Application::background_pump) and not by the frame loop. A native test cannot receive a browser's events, so it plays the browser:
// HiddenDuo seals the host's turns and wakes the application, once per turn, with the time that has passed. (docs/NETWORK_PORT.md, "The browser as a client: hidden tabs, and sound")
// ------------------------------------------------------------------------------------------------------------------------------------------

// A two-player match on TINY: the application is the guest (seat 1) of a bare host (Alice, seat 0); both are in the room, START has run, the match is playing and the page is shown.
// A headless application ends its run by itself after ten frames; `endless_frames` keeps it running for a test that runs many frames.
bool start_two(Peer& host, Application& app, uint32_t seed, bool endless_frames = false) {
    if (!host.net.host(0, "Alice", true)) return false;
    host.net.set_map("TINY.LVL");
    ApplicationConfig cfg = headless_config();
    cfg.net_role = ApplicationConfig::NetRole::Join;
    cfg.net_address = "127.0.0.1";
    cfg.net_port = host.net.listen_port();
    cfg.player_name = "Bob";
    if (endless_frames) {
        cfg.screenshot_path = "ants_hidden_page_unused.bmp";                            // (the countdown of a screenshot run is the other way to end a headless run: it never gets to 1)
        cfg.screenshot_frames = 1 << 30;
    }
    if (!app.init(cfg)) return false;
    Duo duo{app, host};
    if (!duo.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && host.net.can_start(); }, 8000)) return false;
    host.net.set_map("TINY.LVL");
    duo.step(300);
    uint64_t hash = 0;
    if (!net::hash_file(maps_dir() + "TINY.LVL", hash) || !host.net.start_match(seed, hash)) return false;
    if (!duo.until([&]() { return app.state() == AppState::Playing && host.net.phase() == net::NetGame::Phase::Playing; }, 8000)) return false;
    duo.step(6000);                                                                   // the "get ready" dialog takes every key for its five seconds
    return !app.hud().is_modal_open();
}

// The host seals turns for `ms` of game time and nobody polls them on the application's side (the page is not woken)
void host_runs(Peer& host, uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 10) {
        host.now += 10;
        host.update();
        std::this_thread::sleep_for(std::chrono::microseconds(300));
    }
}

// The browser with the page hidden: no frame runs; the host seals a turn every kTurnMs of game time, and each turn that arrives wakes the application with the time since the last one
struct HiddenDuo;
struct VirtualClock;
void advance_clock(VirtualClock* clock, double seconds);

struct HiddenDuo {
    Application& app;
    Peer& peer;
    uint32_t wakes{0};                                  // the wake-ups that made a step
    uint32_t clock_ms{0};
    VirtualClock* clock{nullptr};                       // the application's clock, if the test keeps one: it goes with the game time
    void step(uint32_t ms) {
        for (uint32_t t = 0; t < ms; t += 10) {
            peer.now += 10;
            peer.update();
            std::this_thread::sleep_for(std::chrono::microseconds(300));
            advance_clock(clock, 0.010);
            clock_ms += 10;
            if (clock_ms % net::kTurnMs == 0 && app.background_pump_after(static_cast<float>(net::kTurnMs) / 1000.0f)) ++wakes;
        }
    }
    bool until(const std::function<bool()>& cond, uint32_t max_ms) {
        for (uint32_t t = 0; t < max_ms; t += 10) {
            if (cond()) return true;
            step(10);
        }
        return cond();
    }
};

// The clock of the application's frames and wake-ups (Application::set_clock): time moves when the test moves it. Every rule of the time is then checked exactly and without
// sleeping (a test that sleeps for a window is late on a busy machine). The unit is SDL's performance counter (24 MHz on a Mac, a nanosecond on Linux); it never starts at 0
// (a stamp of 0 means "none yet") and starts a day in, so that a test may set it back by seconds without the unsigned counter wrapping.
struct VirtualClock {
    uint64_t counter{86400ull * SDL_GetPerformanceFrequency()};
    void set(Application& app) { app.set_clock([this]() { return counter; }); }
    static uint64_t units(double seconds) { return static_cast<uint64_t>(seconds * static_cast<double>(SDL_GetPerformanceFrequency()) + 0.5); }
    void advance(double seconds) { counter += units(seconds); }
    void back(double seconds) { counter -= units(seconds); }
};
void advance_clock(VirtualClock* clock, double seconds) {
    if (clock != nullptr) clock->advance(seconds);
}

// The seconds that one turn takes (a frame loop that delivers a frame at least once per turn keeps up with the server by itself)
double turn_seconds() { return static_cast<double>(net::kTurnMs) / 1000.0; }

// The hidden seat has noticed that its host is gone: it elects, it became the host (a two-player match) or the match is over for it
bool host_noticed_gone(Application& app) {
    return app.net()->electing() || app.net()->is_host() || app.net()->phase() != net::NetGame::Phase::Playing;
}

// A hidden page whose frame loop still delivers a frame every `frame_ms` (0: none): the host seals a turn every kTurnMs of play, and each turn wakes the application (a message of
// the server) just as the browser would. `seconds` of play go by on the virtual clock. Returns the ticks that the application executed per second of play.
double hidden_ticks_per_second(Peer& host, Application& app, VirtualClock& vc, uint32_t frame_ms, uint32_t seconds) {
    const uint64_t before = app.sim().current_tick();
    for (uint32_t t = 10; t <= seconds * 1000u; t += 10) {
        vc.advance(0.010);
        host.now += 10;
        host.update();
        std::this_thread::sleep_for(std::chrono::microseconds(300));
        if (frame_ms > 0 && (t + 30) % frame_ms == 0) app.run_frame();                   // a frame comes (its phase is not the turns': they are two clocks)
        if (t % net::kTurnMs == 0) app.background_pump();                                // a message of the server wakes the page (one just after a frame: it stands down)
    }
    return static_cast<double>(app.sim().current_tick() - before) / static_cast<double>(seconds);
}

void run_hidden_page_tests() {
    TEST_CASE("N5.32 Hidden Page: A Network Match Runs On The Wake-Ups Instead Of Frames (Turns Executed And Acknowledged, No Frame, No Picture), The Machines Stay Bit-Identical, The Frame Loop Has The Match Back When The Page Is Shown") {
        Peer host;
        Application app;
        ASSERT_TRUE(start_two(host, app, 31337));
        Duo duo{app, host};
        VirtualClock clock;
        clock.set(app);                                                                   // (the time of the frames and the wake-ups is the test's)
        const float fps_start = app.get_current_fps();
        app.run_frame_with_delta(0.040f);                                                 // a frame of a shown page changes the frame statistics: the probe for "a frame ran"
        const float fps_shown = app.get_current_fps();
        ASSERT_TRUE(fps_shown != fps_start);
        std::random_device unique;                                                        // (a name of its own: two test runs on one machine must not meet at one file)
        const std::string shot = (std::filesystem::temp_directory_path() / ("ants_hidden_page_test_" + std::to_string(unique()) + ".bmp")).string();
        std::remove(shot.c_str());
        app.renderer().request_screenshot(shot);                                          // the next frame that is drawn writes this file

        app.set_page_hidden(true);
        ASSERT_TRUE(app.page_hidden());
        ASSERT_TRUE(app.background_driven());
        const uint32_t executed_before = app.net()->turns_executed();
        const uint64_t tick_at_hide = app.sim().current_tick();
        const uint32_t pumps_at_hide = app.background_pumps();
        ASSERT_TRUE(app.background_pump_after(0.0f));                                     // the frame of the shown page that has just run says nothing about the hidden one: the first wake-up steps
        HiddenDuo hidden{app, host};
        hidden.clock = &clock;
        hidden.step(30000);                                                               // 30 s without one frame
        const uint32_t turns = 30000 / net::kTurnMs;
        ASSERT_EQ(hidden.wakes, turns);
        ASSERT_EQ(app.background_pumps() - pumps_at_hide, turns + 1);
        const uint32_t executed = app.net()->turns_executed() - executed_before;
        ASSERT_TRUE(executed + 4 >= turns && executed <= turns + 1);                      // every turn that the host sealed ran here too, give or take the jitter buffer
        ASSERT_TRUE(host.net.turns_executed() - app.net()->turns_executed() <= 4);
        ASSERT_FALSE(host.net.desynced());                                                // the host compared the hashes that this machine sent while it was hidden
        ASSERT_FALSE(app.net()->desynced());
        ASSERT_EQ(app.net()->phase(), net::NetGame::Phase::Playing);
        ASSERT_EQ(app.get_current_fps(), fps_shown);                                      // no frame ran ...
        ASSERT_FALSE(std::ifstream(shot).good());                                         // ... and nothing was drawn
        const uint32_t pumps = app.background_pumps();
        app.run_frame_with_delta(0.040f);                                                 // a browser that still runs the frames of a page that it calls hidden: the frame is a frame, it draws ...
        ASSERT_TRUE(app.get_current_fps() != fps_shown);
        ASSERT_TRUE(std::ifstream(shot).good());
        std::remove(shot.c_str());
        ASSERT_FALSE(app.background_pump_after(0.1f));                                    // ... and the wake-up stands down while the frames come at least once per turn
        ASSERT_EQ(app.background_pumps(), pumps);
        clock.advance(turn_seconds() - 0.001);
        ASSERT_FALSE(app.background_pump_after(0.1f));                                    // (a hair under a turn after the frame: still standing down)
        ASSERT_EQ(app.background_pumps(), pumps);
        clock.advance(0.002);
        ASSERT_TRUE(app.background_pump_after(0.1f));                                     // a hair over: the frames are slower than the turns, the wake-up drives the match again
        ASSERT_EQ(app.background_pumps(), pumps + 1);

        app.set_page_hidden(false);                                                       // shown again: the frame loop has the match back
        ASSERT_FALSE(app.page_hidden());
        ASSERT_FALSE(app.background_driven());
        {   // the browser's console gets one line about the period: how long, how far the match went, how many wake-ups stepped it
            const std::string line = app.last_hidden_line();
            const uint64_t ticks = app.sim().current_tick() - tick_at_hide;
            ASSERT_TRUE(ticks + 12 >= 30 * 1000 / net::LockstepRunner::kTickMs);                  // (the match ran at its pace: 20 ticks a second, the buffer behind it)
            ASSERT_TRUE(line.rfind("The page was hidden for 30.", 0) == 0);
            ASSERT_TRUE(line.find("; the match advanced by " + std::to_string(ticks) + " ticks and " + std::to_string(app.background_pumps() - pumps_at_hide) +
                                  " wake-ups stepped it in the background") != std::string::npos);
        }
        const float fps_hidden_frame = app.get_current_fps();
        app.renderer().request_screenshot(shot);
        app.run_frame_with_delta(0.016f);
        ASSERT_TRUE(app.get_current_fps() != fps_hidden_frame);
        ASSERT_TRUE(std::ifstream(shot).good());                                          // and draws again
        std::remove(shot.c_str());
        host.net.freeze();                                                                // the host stops sealing; both machines run what they have and stand still
        duo.step(2000);
        ASSERT_EQ(app.net()->turns_executed(), host.net.turns_executed());
        ASSERT_EQ(app.sim().current_tick(), host.sim.current_tick());
        ASSERT_TRUE(app.sim().state_hash() == host.sim.state_hash());                     // the same game, tick for tick, hash for hash
    } TEST_END();

    TEST_CASE("N5.33 Hidden Page: One Driver At A Time - The Wake-Up Of A Shown Page Does Nothing, The Wake-Ups Of A Hidden Page Stand Down While Its Frames Come At Least Once Per Turn, No Step Starts Inside Another") {
        Peer host;
        Application app;
        ASSERT_TRUE(start_two(host, app, 4242));
        VirtualClock clock;
        clock.set(app);
        host_runs(host, 5 * net::kTurnMs);                                                // shown: five turns arrive and nobody polls them yet
        const uint32_t waiting = app.net()->turns_executed();
        ASSERT_FALSE(app.background_pump_after(0.5f));                                    // a wake-up of a shown page does nothing, whatever it says
        ASSERT_FALSE(app.background_pump());
        ASSERT_EQ(app.net()->turns_executed(), waiting);
        ASSERT_EQ(app.background_pumps(), 0u);

        app.set_page_hidden(true);
        const float fps = app.get_current_fps();
        ASSERT_TRUE(app.background_pump_after(0.100f));                                   // a hidden page: the wake-up drives the match, the turns that waited run, no frame
        ASSERT_TRUE(app.net()->turns_executed() > waiting);
        ASSERT_EQ(app.background_pumps(), 1u);
        ASSERT_EQ(app.get_current_fps(), fps);
        app.run_frame_with_delta(0.050f);                                                 // a frame in a hidden page (the browser still draws it): the frame loop drives the match ...
        ASSERT_TRUE(app.get_current_fps() != fps);
        ASSERT_FALSE(app.background_pump_after(0.1f));                                    // ... and the wake-up stands down while frames come (less than a turn after the last)
        ASSERT_FALSE(app.background_pump());
        ASSERT_EQ(app.background_pumps(), 1u);
        clock.advance(0.3);

        uint32_t inside = 0;                                                              // no step starts inside another: from a tick of a step every way in is refused
        uint32_t refused = 0;
        app.net()->set_on_tick([&]() {
            const uint32_t before = app.background_pumps();
            ++inside;
            if (!app.background_pump_after(0.1f)) ++refused;
            if (!app.background_pump()) ++refused;
            app.run_frame_with_delta(0.1f);                                               // (it has no result: it must change nothing)
            if (app.background_pumps() == before) ++refused;
        });
        host_runs(host, 3 * net::kTurnMs);
        const uint32_t pumps = app.background_pumps();
        ASSERT_TRUE(app.background_pump_after(0.2f));
        ASSERT_EQ(app.background_pumps(), pumps + 1);                                     // the outer step counts once, whatever was tried inside it
        ASSERT_TRUE(inside >= 2);                                                         // ticks ran, so the nested attempts happened
        ASSERT_EQ(refused, inside * 3);                                                   // and every one of them was refused

        app.set_page_hidden(false);                                                       // a shown page: a frame never starts inside a frame, and nothing wakes it from inside either
        uint32_t leaked = 0;
        uint32_t ran_inside = 0;
        app.net()->set_on_tick([&]() {
            ++ran_inside;
            const float before = app.get_current_fps();
            app.run_frame_with_delta(0.1f);                                               // refused: it would change the frame statistics a second time
            if (app.get_current_fps() != before) ++leaked;
            if (app.background_pump_after(0.1f)) ++leaked;
        });
        host_runs(host, 3 * net::kTurnMs);
        app.run_frame_with_delta(0.050f);
        ASSERT_TRUE(ran_inside >= 1);
        ASSERT_EQ(leaked, 0u);
    } TEST_END();

    TEST_CASE("N5.34 Hidden Page: The Time Accounting Of The Wake-Ups - A Wake-Up Gives The Network At Most A Second And Never Less Than Nothing (A Page That Was Asleep Is Not Paid Back At Once), Frames And Wake-Ups Share One Timer") {
        {   // a live host and a long pause: the network clock and the runner get one second, whatever the page says
            Peer host;
            Application app;
            ASSERT_TRUE(start_two(host, app, 777));
            app.set_page_hidden(true);
            host_runs(host, 30 * net::kTurnMs);                                           // thirty turns wait
            bool every_wake_stepped = true;
            const auto given = [&](float elapsed) {                                       // what one wake-up of `elapsed` seconds gives the network clock (ms)
                const double before = app.net_clock_ms();
                if (!app.background_pump_after(elapsed)) every_wake_stepped = false;
                return app.net_clock_ms() - before;
            };
            const uint32_t turns_before = app.net()->turns_executed();
            const double ten_minutes = given(600.0f);
            ASSERT_TRUE(std::abs(ten_minutes - 1000.0) < 0.001);                          // ten minutes of "time" give exactly one second ...
            const uint32_t ran = app.net()->turns_executed() - turns_before;
            ASSERT_TRUE(ran >= 3 && ran <= 4 * 1000 / net::kTurnMs);                      // ... which the runner pays out under its own limits: at most the ticks that that second holds at its fastest speed (it runs a backlog down at up to 4x, also in a long step)
            ASSERT_EQ(app.net()->phase(), net::NetGame::Phase::Playing);                  // (the host is alive: what waited in the link was read first, it is no silence)
            ASSERT_FALSE(app.net()->electing());
            ASSERT_FALSE(app.net()->is_host());
            ASSERT_TRUE(std::abs(given(5.0f) - 1000.0) < 0.001);                          // five seconds: one (a cap of eight seconds would let all five through)
            ASSERT_TRUE(std::abs(given(1.0f) - 1000.0) < 0.001);                          // the cap itself
            ASSERT_TRUE(std::abs(given(0.4f) - 400.0) < 0.01);                            // less than the cap counts in full (a cap of 0.3 would cut it)
            ASSERT_TRUE(std::abs(given(0.05f) - 50.0) < 0.01);
            ASSERT_TRUE(std::abs(given(0.0f)) < 0.001);
            ASSERT_TRUE(std::abs(given(-5.0f)) < 0.001);                                  // a clock that went backwards counts for nothing: it must not wrap the network's clock
            ASSERT_TRUE(every_wake_stepped);
            ASSERT_EQ(app.net()->phase(), net::NetGame::Phase::Playing);
            ASSERT_FALSE(app.net()->electing());
            ASSERT_FALSE(app.net()->is_host());                                           // (a clock that wrapped made the session think the host had been silent for ever)
            ASSERT_FALSE(app.net()->desynced());
        }
        {   // frames and wake-ups share one timer: the time between two of them is counted once
            Peer host;
            Application app;
            ASSERT_TRUE(start_two(host, app, 779));
            VirtualClock clock;
            clock.set(app);
            app.run_frame();                                                              // (the frame loop's timer starts)
            app.set_page_hidden(true);
            clock.advance(0.250);
            const double before = app.net_clock_ms();
            ASSERT_TRUE(app.background_pump());                                           // the wake-up counts the quarter second ...
            ASSERT_TRUE(std::abs(app.net_clock_ms() - before - 250.0) < 0.01);
            clock.advance(0.040);
            app.set_page_hidden(false);
            const double frame_before = app.net_clock_ms();                               // (the page is shown: the last wake-up of set_page_hidden counted the 40 ms)
            ASSERT_TRUE(std::abs(frame_before - before - 290.0) < 0.01);
            clock.advance(0.030);
            app.run_frame();                                                              // ... and the frame after it measures from there, not from the frame before: 30 ms and no more
            ASSERT_TRUE(std::abs(app.net_clock_ms() - frame_before - 30.0) < 0.5);
        }
    } TEST_END();

    TEST_CASE("N5.35 Hidden Page: The Hidden Hours Are Not One Huge Frame - The First Frame After The Page Is Shown Measures From The Moment It Was Shown") {
        Application app;
        ASSERT_TRUE(app.init(headless_config()));
        ASSERT_TRUE(app.start_game("Original-Ants/Maps/SMALL.LVL"));
        VirtualClock clock;
        clock.set(app);
        app.run_frame();                                                                  // the first frame starts the frame loop's timer
        float before = app.get_current_fps();
        clock.advance(0.25);
        app.run_frame();                                                                  // the control: a quarter second between two frames is one frame of 100 ms (the clamp), the rate drops
        ASSERT_TRUE(app.get_current_fps() < before - 3.0f);
        before = app.get_current_fps();
        app.set_page_hidden(true);
        clock.advance(0.25);                                                              // the same quarter second, while the page is hidden
        app.set_page_hidden(false);
        app.run_frame();                                                                  // is no frame at all
        ASSERT_TRUE(app.get_current_fps() > before - 3.0f);
        app.set_page_hidden(true);
        clock.advance(3 * 3600.0);                                                        // and three hours hidden are no frame either
        app.set_page_hidden(false);
        app.run_frame();
        ASSERT_TRUE(app.get_current_fps() > before - 3.0f);
    } TEST_END();

    TEST_CASE("N5.36 Hidden Page: No Sound Effects Are Played While The Page Is Hidden (The Events Are Dropped, Not Saved Up), The Sound Is Back When It Is Shown") {
        Peer host;
        Application app;
        ASSERT_TRUE(start_two(host, app, 9090));
        Duo duo{app, host};
        Command hatch;                                                                    // "can't hatch" (the score is below 200): the cue of the clicking machine
        hatch.type = CommandType::Hatch;
        const size_t quiet = app.audio_mixer().active_channel_count();                    // (the match's start voice plays in a shown page: nothing mixes without a device, so it stays)
        ASSERT_TRUE(quiet >= 1);
        app.set_page_hidden(true);
        HiddenDuo hidden{app, host};
        ASSERT_EQ(app.net()->submit(hatch).status, sim::CommandResult::Status::Applied);
        hidden.step(1500);
        ASSERT_TRUE(app.sim().poll_audio_events().empty());                               // the queue of a page that nobody hears is drained all the same, it must not grow
        ASSERT_EQ(app.audio_mixer().active_channel_count(), quiet);                       // and nothing was played
        ASSERT_EQ(hidden.wakes, 1500 / net::kTurnMs);                                     // (the wake-ups did step: 1.5 s of turns)
        ASSERT_EQ(app.net()->submit(hatch).status, sim::CommandResult::Status::Applied);  // a hidden page whose frames run all the same (the browser still draws it) keeps its sound:
        host_runs(host, 2 * net::kTurnMs);                                                // the ticks of a frame are not a background step
        for (int i = 0; i < 4 && app.audio_mixer().active_channel_count() == quiet; ++i) app.run_frame_with_delta(0.1f);        // (a frame runs one turn)
        ASSERT_TRUE(app.audio_mixer().active_channel_count() > quiet);
        app.audio_mixer().stop_all();
        app.set_page_hidden(false);
        ASSERT_EQ(app.audio_mixer().active_channel_count(), 0u);
        ASSERT_EQ(app.net()->submit(hatch).status, sim::CommandResult::Status::Applied);
        duo.step(1500);
        ASSERT_TRUE(app.audio_mixer().active_channel_count() > 0u);                       // the same cue is heard in a page that is shown
        {   // the sounds that no tick makes (a button, the can't-go cue) are silent in a step too: called from inside a step (a tick of it) they play nothing ...
            Peer host2;
            Application app2;
            ASSERT_TRUE(start_two(host2, app2, 9191));
            Duo duo2{app2, host2};
            const size_t quiet2 = app2.audio_mixer().active_channel_count();
            uint32_t ticks_seen = 0;
            app2.net()->set_on_tick([&]() {
                ++ticks_seen;
                app2.hud().play_sfx(sim::SoundID::ButtonClick);
                app2.map_select().play_sfx(sim::SoundID::CantGo);
                app2.scorecard().play_sfx(sim::SoundID::NavButtonClick);
            });
            app2.set_page_hidden(true);
            HiddenDuo hidden2{app2, host2};
            hidden2.step(600);
            ASSERT_TRUE(ticks_seen >= 4);
            ASSERT_EQ(app2.audio_mixer().active_channel_count(), quiet2);
            app2.set_page_hidden(false);
            duo2.step(600);
            ASSERT_TRUE(ticks_seen >= 8);
            ASSERT_TRUE(app2.audio_mixer().active_channel_count() > quiet2);              // ... and in a page that is shown the same calls are heard
        }
    } TEST_END();

    TEST_CASE("N5.37 Hidden Page: When The Page Is Shown The Last Wake-Up Brings The Clocks Up To Date, Then The Frame Loop Has The Match And Nothing Steps Twice") {
        Peer host;
        Application app;
        ASSERT_TRUE(start_two(host, app, 2468));
        VirtualClock clock;
        clock.set(app);
        app.run_frame();                                                                  // (the frame loop's timer starts)
        app.set_page_hidden(true);
        host_runs(host, 4 * net::kTurnMs);                                                // four turns are on their way and nobody woke the page
        clock.advance(0.3);
        const uint32_t before = app.net()->turns_executed();
        ASSERT_EQ(app.background_pumps(), 0u);
        app.set_page_hidden(false);
        ASSERT_TRUE(app.net()->turns_executed() > before);                                // the turns that waited ran at the moment the page was shown
        ASSERT_EQ(app.background_pumps(), 1u);
        ASSERT_FALSE(app.background_driven());
        ASSERT_FALSE(app.background_pump_after(0.1f));                                    // nothing steps in the background any more
        app.set_page_hidden(false);                                                       // (shown twice is shown once)
        ASSERT_EQ(app.background_pumps(), 1u);
    } TEST_END();

    TEST_CASE("N5.38 Hidden Page: A Local Game Is Not Driven By Wake-Ups - It Stands Still With The Page, Frames Run As Before; A Page That Was Never Told Is Shown") {
        Application app;
        ASSERT_TRUE(app.init(headless_config()));
        ASSERT_FALSE(app.page_hidden());
        ASSERT_TRUE(app.start_game("Original-Ants/Maps/SMALL.LVL"));
        app.set_page_hidden(true);
        ASSERT_TRUE(app.page_hidden());
        ASSERT_FALSE(app.background_driven());                                            // no room, no match of the network
        ASSERT_FALSE(app.background_pump_after(1.0f));
        ASSERT_FALSE(app.background_pump());
        ASSERT_TRUE(app.hud().is_match_start_modal_active() && app.hud().match_start_modal_ticks() == 0u && app.sim().current_tick() == 0u);   // the "Get ready" dialog of a local game: nothing of it passed in the wake-ups
        const float fps = app.get_current_fps();
        app.run_frame_with_delta(0.040f);                                                 // the frame loop is the only driver of a local game
        ASSERT_TRUE(app.get_current_fps() != fps);
        ASSERT_EQ(app.background_pumps(), 0u);
        ASSERT_EQ(app.hud().match_start_modal_ticks(), 0u);                               // (40 ms of the frame clock are not a whole step of 50 ms yet)
        app.run_frame_with_delta(0.040f);
        ASSERT_TRUE(app.hud().match_start_modal_ticks() == 1u && app.sim().current_tick() == 0u);   // the frames count the dialog, 50 ms a step, and the simulation waits for it
        app.set_page_hidden(false);
        ASSERT_FALSE(app.page_hidden());
    } TEST_END();

    TEST_CASE("N5.39 Hidden Page: A Page That Is Hidden Before The Match Starts Is Driven Through The Room, The Start And The Match By The Wake-Ups Alone") {
        Peer host;
        ASSERT_TRUE(host.net.host(0, "Alice", true));
        host.net.set_map("TINY.LVL");
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Join;
        cfg.net_address = "127.0.0.1";
        cfg.net_port = host.net.listen_port();
        cfg.player_name = "Bob";
        Application app;
        ASSERT_TRUE(app.init(cfg));
        app.set_page_hidden(true);                                                        // a background tab: the page never draws a frame
        HiddenDuo hidden{app, host};
        ASSERT_TRUE(hidden.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && host.net.can_start(); }, 8000));
        ASSERT_EQ(app.net()->my_seat(), 1);
        host.net.set_map("TINY.LVL");
        hidden.step(300);
        uint64_t hash = 0;
        ASSERT_TRUE(net::hash_file(maps_dir() + "TINY.LVL", hash));
        ASSERT_TRUE(host.net.start_match(5150, hash));
        ASSERT_TRUE(hidden.until([&]() { return app.state() == AppState::Playing && host.net.phase() == net::NetGame::Phase::Playing; }, 8000));
        ASSERT_EQ(app.local_player_id(), 1);
        ASSERT_EQ(app.audio_mixer().active_channel_count(), 0u);                          // a match that begins in a hidden page has no start voice (the shown page's has one: N5.36)
        // the "Get ready" dialog of a hidden page ends on time as well: with the first turn that executes, which a wake-up runs (post_tick), kDialogMs after the match began (the host seals
        // the first turn that long after it) and before the second turn's time: a page that never draws a frame does not need one to start its match
        ASSERT_TRUE(app.hud().is_match_start_modal_active() && app.sim().current_tick() == 0);
        uint32_t waited = 0;
        while (app.hud().is_match_start_modal_active() && waited < 20000) {
            hidden.step(10);
            waited += 10;
        }
        ASSERT_FALSE(app.hud().is_match_start_modal_active());
        ASSERT_TRUE(app.sim().current_tick() >= 1);
        ASSERT_TRUE(waited >= kDialogMs - 20 && waited <= kDialogMs + 150);               // (the Begin was read a step or two before the check above)
        hidden.step(10000);
        ASSERT_TRUE(app.net()->turns_executed() + 6 >= 10000 / net::kTurnMs);             // the match runs here although no frame ever did
        ASSERT_FALSE(host.net.desynced());
        ASSERT_FALSE(app.net()->desynced());
        ASSERT_EQ(app.net()->phase(), net::NetGame::Phase::Playing);
    } TEST_END();

    TEST_CASE("N5.40 Hidden Page: Nothing Starts Or Changes The Music While The Page Is Hidden - A Match That Begins, Ends Or Is Lost In A Hidden Page Leaves Its Music For When The Page Is Shown, Once") {
        const std::string intro = "Original-Ants/INTRO.mp3";
        const auto in_game = [](const std::string& file) {
            return file == "Original-Ants/ANTS2A.mp3" || file == "Original-Ants/ANTS2B.mp3" || file == "Original-Ants/ANTSFUN3.mp3";
        };
        // a match that begins while the page is hidden: the intro of the setup screen plays on, the match's music starts when the page is shown (or at the next frame, when the
        // hidden page still draws: a frame is what a page that is heard runs). `mode`: 0 the page is shown; 1 a frame of the hidden page comes first; 2 the Begin of the match
        // waits in the link and is read by the very last wake-up that the page makes when it is shown: its music starts then, not at some later frame
        const auto match_begins_hidden = [&](int mode, uint32_t seed) {
            Peer host;
            ASSERT_TRUE(host.net.host(0, "Alice", true));
            host.net.set_map("TINY.LVL");
            ApplicationConfig cfg = headless_config();
            cfg.net_role = ApplicationConfig::NetRole::Join;
            cfg.net_address = "127.0.0.1";
            cfg.net_port = host.net.listen_port();
            cfg.player_name = "Bob";
            Application app;
            ASSERT_TRUE(app.init(cfg));
            ASSERT_EQ(app.audio_mixer().music_filepath(), intro);                         // the setup screen plays the intro
            app.set_page_hidden(true);
            HiddenDuo hidden{app, host};
            ASSERT_TRUE(hidden.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && host.net.can_start(); }, 8000));
            host.net.set_map("TINY.LVL");
            hidden.step(300);
            uint64_t hash = 0;
            ASSERT_TRUE(net::hash_file(maps_dir() + "TINY.LVL", hash));
            ASSERT_TRUE(host.net.start_match(seed, hash));
            if (mode == 2) {
                ASSERT_TRUE(hidden.until([&]() { return app.net()->phase() == net::NetGame::Phase::Loading; }, 8000));      // the Start is handled: the map is loaded and reported
                host_runs(host, 300);                                                     // the host takes the report and begins the match; nobody wakes the page
                ASSERT_EQ(app.state(), AppState::MapSelect);
                ASSERT_EQ(app.audio_mixer().music_filepath(), intro);
                app.set_page_hidden(false);                                               // the last wake-up of the hidden page reads the Begin ...
                ASSERT_EQ(app.state(), AppState::Playing);
                ASSERT_TRUE(in_game(app.audio_mixer().music_filepath()));                 // ... and the music of the match is playing when the page is shown, not at some frame after it
                ASSERT_TRUE(app.audio_mixer().is_music_playing());
                return;
            }
            ASSERT_TRUE(hidden.until([&]() { return app.state() == AppState::Playing && host.net.phase() == net::NetGame::Phase::Playing; }, 8000));
            ASSERT_EQ(app.audio_mixer().music_filepath(), intro);                         // the match began and nothing was started: the intro plays on
            hidden.step(3000);
            ASSERT_EQ(app.audio_mixer().music_filepath(), intro);
            if (mode == 1) {
                app.run_frame_with_delta(0.016f);                                         // a frame of the hidden page: the music of the match starts now ...
                ASSERT_TRUE(in_game(app.audio_mixer().music_filepath()));
                ASSERT_TRUE(app.audio_mixer().is_music_playing());
            }
            const std::string before_shown = app.audio_mixer().music_filepath();
            app.set_page_hidden(false);
            const std::string piece = app.audio_mixer().music_filepath();
            ASSERT_TRUE(in_game(piece));                                                  // the page is shown: the music of the match is playing
            ASSERT_TRUE(app.audio_mixer().is_music_playing());
            if (mode == 1) ASSERT_EQ(piece, before_shown);                                // (and showing the page did not start it a second time)
            app.set_page_hidden(true);                                                    // once: a hidden period in which nothing happens starts nothing
            hidden.step(1000);
            app.set_page_hidden(false);
            ASSERT_EQ(app.audio_mixer().music_filepath(), piece);                         // (a second start would pick another piece: never the one that played last)
            app.run_frame_with_delta(0.016f);                                             // and the frame that follows does not start it again
            ASSERT_EQ(app.audio_mixer().music_filepath(), piece);
        };
        match_begins_hidden(0, 4711);
        match_begins_hidden(1, 4712);
        match_begins_hidden(2, 4713);
        {   // the match ends while the page is hidden: the music closes when the page is shown (the original closes it at the end of a match)
            Peer host;
            Application app;
            ASSERT_TRUE(start_two(host, app, 8181));
            const std::string piece = app.audio_mixer().music_filepath();
            ASSERT_TRUE(in_game(piece));
            app.set_page_hidden(true);
            HiddenDuo hidden{app, host};
            hidden.step(500);
            Command quit;
            quit.type = CommandType::Quit;
            quit.issuer = 0;
            ASSERT_EQ(host.net.submit(quit).status, sim::CommandResult::Status::Applied);
            ASSERT_TRUE(hidden.until([&]() { return app.scorecard().is_open(); }, 5000));
            hidden.step(500);
            ASSERT_EQ(app.audio_mixer().music_filepath(), piece);                         // the match is over, the results are up, and the music was not touched
            ASSERT_TRUE(app.audio_mixer().is_music_playing());
            app.set_page_hidden(false);
            ASSERT_FALSE(app.audio_mixer().is_music_playing());                           // the page is shown: it closes
        }
        {   // the server is lost while the page is hidden: the setup screen is back, its intro starts when the page is shown
            auto server = std::make_unique<Server>();
            ASSERT_TRUE(server->make_room("HIDDEN-LOST", 2));
            ApplicationConfig cfg = headless_config();
            cfg.net_role = ApplicationConfig::NetRole::Join;
            cfg.net_address = "127.0.0.1";
            cfg.net_port = server->port();
            cfg.net_room = "HIDDEN-LOST";
            cfg.player_name = "Leader";
            Application app;
            ASSERT_TRUE(app.init(cfg));
            Peer bob;
            ASSERT_TRUE(bob.net.join("127.0.0.1", server->port(), "Bob", 255, "HIDDEN-LOST"));
            {
                Hall hall{*server, &app, {&bob}};
                ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 15000));
                hall.step(500);
            }
            const std::string piece = app.audio_mixer().music_filepath();
            ASSERT_TRUE(in_game(piece));
            app.set_page_hidden(true);
            uint32_t clock_ms = 0;
            const auto hidden_step = [&](uint32_t ms) {                                   // the other machines run and each turn of the server wakes the page
                for (uint32_t t = 0; t < ms; t += 10) {
                    bob.now += 10;
                    bob.update();
                    if (server) {
                        server->now += 10;
                        server->update();
                    }
                    std::this_thread::sleep_for(std::chrono::microseconds(300));
                    clock_ms += 10;
                    if (clock_ms % net::kTurnMs == 0) app.background_pump_after(static_cast<float>(turn_seconds()));
                }
            };
            hidden_step(1000);
            ASSERT_EQ(app.state(), AppState::Playing);
            server.reset();                                                               // the server goes away under the hidden page: its connection closes
            for (int i = 0; i < 300 && app.state() != AppState::MapSelect; ++i) hidden_step(10);
            ASSERT_EQ(app.state(), AppState::MapSelect);                                  // the step noticed it: the setup screen is back ...
            ASSERT_EQ(app.audio_mixer().music_filepath(), piece);                         // ... and no music was started: the piece of the match plays on
            app.set_page_hidden(false);
            ASSERT_EQ(app.audio_mixer().music_filepath(), intro);                         // the page is shown: the intro of the setup screen starts
            ASSERT_TRUE(app.audio_mixer().is_music_playing());
        }
    } TEST_END();

    TEST_CASE("N5.41 Hidden Page: A Seat Whose Hidden Page Still Gets A Few Frames A Second Keeps Up With The Turns (The Frames Are Too Few To Carry Them: The Wake-Ups Step Between Them)") {
        const struct { uint32_t frame_ms; const char* what; } rates[] = {
            {1000, "a frame a second"}, {250, "four frames a second"}, {220, "a frame every 220 ms"}, {20, "fifty frames a second"}, {0, "no frame"},
        };
        for (const auto& rate : rates) {
            Peer host;
            Application app;
            ASSERT_TRUE(start_two(host, app, 6161, true));
            VirtualClock clock;
            clock.set(app);
            app.run_frame();                                                              // (the frame loop's timer starts)
            app.set_page_hidden(true);
            const uint32_t pumps_before = app.background_pumps();
            const double per_second = hidden_ticks_per_second(host, app, clock, rate.frame_ms, 20);
            if (!(per_second >= 19.0 && per_second <= 21.0)) std::cout << "\n    [" << rate.what << ": " << per_second << " ticks a second]\n";
            ASSERT_TRUE(per_second >= 19.0 && per_second <= 21.0);                        // 20 ticks a second of play, as the others have them (give or take the buffer)
            const uint32_t pumps = app.background_pumps() - pumps_before;
            if (rate.frame_ms > 0 && rate.frame_ms <= net::kTurnMs / 2) ASSERT_EQ(pumps, 0u);                // frames at least once per turn carry the match (with their sound): the wake-ups stand down
            if (rate.frame_ms == 0 || rate.frame_ms >= 2 * net::kTurnMs) ASSERT_TRUE(pumps >= 20 * 1000 / net::kTurnMs / 2);   // a slow frame loop (or none) cannot: the wake-ups carry it
            ASSERT_FALSE(host.net.desynced());
            ASSERT_FALSE(app.net()->desynced());
            ASSERT_TRUE(host.net.turns_executed() - app.net()->turns_executed() <= 6);    // not left behind: the server's sequencer would have waited for it
            ASSERT_EQ(app.net()->phase(), net::NetGame::Phase::Playing);
        }
    } TEST_END();

    TEST_CASE("N5.42 Hidden Page: The Silence Of The Host Is Real Time - A Host That Was Asked And Does Not Answer Is Called Silent By The Wake-Up After Its Ten Seconds, A Host With Data Waiting Or One That Answers (Even When It Holds Its Turns) Is Never Taken For Silent") {
        {   // a host that stops: the page is woken once a minute (a throttled timer). The first wake-up asks it (it has not been asked since it spoke), the next one decides
            Peer host;
            Application app;
            ASSERT_TRUE(start_two(host, app, 5151));
            VirtualClock clock;
            clock.set(app);
            app.set_page_hidden(true);
            host_runs(host, 2 * net::kTurnMs);                                            // the last that the host says ...
            ASSERT_TRUE(app.background_pump());                                           // ... is read here: the silence starts now
            clock.advance(60.0);
            ASSERT_TRUE(app.background_pump());                                           // a minute of nothing, in ONE wake-up: nobody has asked the host since it spoke (a sleeping page pings
            ASSERT_EQ(app.net()->phase(), net::NetGame::Phase::Playing);                  // nobody), so it is asked now and not condemned
            ASSERT_FALSE(host_noticed_gone(app));
            clock.advance(2.9);
            ASSERT_TRUE(app.background_pump());                                           // 2.9 s after the question: the host has had less than the grace (3 s) to answer
            ASSERT_FALSE(host_noticed_gone(app));
            clock.advance(0.2);
            ASSERT_TRUE(app.background_pump());                                           // 3.1 s: it has not answered
            ASSERT_TRUE(host_noticed_gone(app));
        }
        {   // the same when the page sleeps ten minutes between its two wake-ups
            Peer host;
            Application app;
            ASSERT_TRUE(start_two(host, app, 5152));
            VirtualClock clock;
            clock.set(app);
            app.set_page_hidden(true);
            host_runs(host, 2 * net::kTurnMs);
            ASSERT_TRUE(app.background_pump());
            clock.advance(600.0);
            ASSERT_TRUE(app.background_pump());                                           // asks
            ASSERT_FALSE(host_noticed_gone(app));
            clock.advance(600.0);
            ASSERT_TRUE(app.background_pump());                                           // the host did not answer in ten minutes: silent
            ASSERT_TRUE(host_noticed_gone(app));
        }
        {   // wake-ups every second and a half (a throttled page that still gets a timer): each hands the session half a second that the clock did not count, and the first after ten seconds
            Peer host;                                                                    // of silence notices (the host is asked at the first of them and the gap counts in full from then on)
            Application app;
            ASSERT_TRUE(start_two(host, app, 5155));
            VirtualClock clock;
            clock.set(app);
            app.set_page_hidden(true);
            host_runs(host, 2 * net::kTurnMs);
            ASSERT_TRUE(app.background_pump());
            for (int i = 0; i < 6; ++i) {                                                 // 9 s of silence
                clock.advance(1.5);
                ASSERT_TRUE(app.background_pump());
                ASSERT_FALSE(host_noticed_gone(app));
            }
            clock.advance(1.5);
            ASSERT_TRUE(app.background_pump());                                           // 10.5 s
            ASSERT_TRUE(host_noticed_gone(app));
        }
        {   // a live host with data waiting: its turns and pings wait in the link while the page sleeps, and the wake-up reads them first. Once a minute, for three minutes: never silent
            Peer host;
            Application app;
            ASSERT_TRUE(start_two(host, app, 5153));
            VirtualClock clock;
            clock.set(app);
            app.set_page_hidden(true);
            ASSERT_TRUE(app.background_pump());
            for (int minute = 0; minute < 3; ++minute) {
                host_runs(host, 2000);                                                    // (the host's own clock: it is alive, it says things)
                clock.advance(60.0);
                const uint32_t turns_before = app.net()->turns_executed();
                ASSERT_TRUE(app.background_pump());
                ASSERT_TRUE(app.net()->turns_executed() > turns_before);                  // what waited ran (a second's worth of it)
                ASSERT_EQ(app.net()->phase(), net::NetGame::Phase::Playing);
                ASSERT_FALSE(app.net()->electing());
                ASSERT_FALSE(app.net()->is_host());
                ASSERT_FALSE(host_noticed_gone(app));
            }
            ASSERT_FALSE(host.net.desynced());
            ASSERT_FALSE(app.net()->desynced());
        }
        {   // a live host that holds its turns (a server that waits for a player who lags, a match that is paused) is quiet but answers pings: the wake-up asks, the answer is a
            Peer host;                                                                    // message that wakes the page and is heard, and the host is never taken for silent
            Application app;
            ASSERT_TRUE(start_two(host, app, 5156));
            VirtualClock clock;
            clock.set(app);
            app.set_page_hidden(true);
            host.net.freeze();                                                            // nothing is sealed any more: only the answers to pings come
            host_runs(host, 2 * net::kTurnMs);
            ASSERT_TRUE(app.background_pump());
            for (int minute = 0; minute < 3; ++minute) {
                clock.advance(60.0);
                ASSERT_TRUE(app.background_pump());                                       // the timer's wake-up: asks
                ASSERT_FALSE(host_noticed_gone(app));
                host_runs(host, 200);                                                     // the host answers ...
                ASSERT_TRUE(app.background_pump());                                       // ... and the answer wakes the page
                ASSERT_FALSE(host_noticed_gone(app));
            }
            ASSERT_EQ(app.net()->phase(), net::NetGame::Phase::Playing);
            ASSERT_FALSE(app.net()->is_host());
        }
        {   // a shown page is not touched: its frames hand the session no gap (a frame counts at most a second), whatever the clock does between two of them
            Peer host;
            Application app;
            ASSERT_TRUE(start_two(host, app, 5154, true));
            VirtualClock clock;
            clock.set(app);
            app.run_frame();
            for (int i = 0; i < 3; ++i) {                                                 // the host says nothing (nobody runs it) and 20 s pass between the frames
                clock.advance(20.0);
                app.run_frame();
                ASSERT_FALSE(host_noticed_gone(app));
            }
            ASSERT_TRUE(app.net_clock_ms() > 0.0);
        }
    } TEST_END();

    TEST_CASE("N5.43 Hidden Page: A Wake-Up After The Application Has Stopped Does Nothing, And A Clock That Goes Backwards Counts For Nothing") {
        {
            Peer host;
            Application app;
            ASSERT_TRUE(start_two(host, app, 3131));
            VirtualClock clock;
            clock.set(app);
            app.set_page_hidden(true);
            ASSERT_TRUE(app.background_pump());
            for (int i = 0; i < 12 && app.is_running(); ++i) app.run_frame_with_delta(0.016f);   // a headless run ends by itself after ten frames
            ASSERT_FALSE(app.is_running());
            ASSERT_TRUE(app.network_active());                                            // (the network is still there: the stop is what keeps the wake-up out)
            ASSERT_TRUE(app.page_hidden());
            const uint32_t pumps = app.background_pumps();
            const double network_clock = app.net_clock_ms();
            clock.advance(1.0);
            ASSERT_FALSE(app.background_pump_after(0.1f));
            ASSERT_FALSE(app.background_pump());
            ASSERT_EQ(app.background_pumps(), pumps);
            ASSERT_TRUE(std::abs(app.net_clock_ms() - network_clock) < 0.001);
        }
        {
            Peer host;
            Application app;
            ASSERT_TRUE(start_two(host, app, 3132));
            VirtualClock clock;
            clock.set(app);
            app.set_page_hidden(true);
            host_runs(host, 2 * net::kTurnMs);
            clock.advance(2.0);
            ASSERT_TRUE(app.background_pump());
            const double network_clock = app.net_clock_ms();
            clock.back(5.0);                                                              // the clock goes back five seconds
            ASSERT_TRUE(app.background_pump());
            ASSERT_TRUE(std::abs(app.net_clock_ms() - network_clock) < 0.001);            // it counts for nothing (and does not wrap the network's clock)
            ASSERT_FALSE(app.net()->is_host());
            ASSERT_FALSE(host_noticed_gone(app));
            clock.advance(0.5);
            ASSERT_TRUE(app.background_pump());                                           // and the time goes on from the moment it was set back to
            ASSERT_TRUE(std::abs(app.net_clock_ms() - network_clock - 500.0) < 0.01);
            app.run_frame_with_delta(0.016f);                                             // a frame of the hidden page ... and the clock goes back behind it: the stamp of that frame
            clock.back(5.0);                                                              // says nothing any more, the frames are not taken to be in charge, a driver is needed
            ASSERT_TRUE(app.background_pump());
            clock.advance(0.2);
            ASSERT_TRUE(app.background_pump());
            ASSERT_FALSE(host_noticed_gone(app));
        }
    } TEST_END();

    TEST_CASE("N5.44 Hidden Page: The Console's Line About A Hidden Period Says What Happened (A Room Is Not A Match That Advanced By 0 Ticks) And Is Not Repeated By A Tab That Is Flicked Through") {
        ASSERT_EQ(Application::hidden_period_line(0.5, 100, 10, 1.0e9), std::string());                               // shorter than a second
        ASSERT_EQ(Application::hidden_period_line(61.24, 1224, 672, 1.0e9),
                  std::string("The page was hidden for 61.2 s; the match advanced by 1224 ticks and 672 wake-ups stepped it in the background"));
        ASSERT_EQ(Application::hidden_period_line(25.0, 0, 51, 1.0e9),
                  std::string("The page was hidden for 25.0 s; 51 wake-ups kept the connection going in the background (no match tick ran)"));
        ASSERT_EQ(Application::hidden_period_line(30.0, 600, 0, 1.0e9), std::string());                               // no wake-up stepped: nothing to tell
        ASSERT_EQ(Application::hidden_period_line(30.0, 600, 10, 9.9), std::string());                                // another line less than ten seconds ago
        ASSERT_FALSE(Application::hidden_period_line(30.0, 600, 10, 10.1).empty());
        ASSERT_TRUE(Application::hidden_period_line(1.0, 20, 5, 1.0e9).find("1.0 s") != std::string::npos);
        // a period in a room: the wake-ups answer the server, no tick runs, and the line does not claim that a match advanced
        Peer host;
        ASSERT_TRUE(host.net.host(0, "Alice", true));
        host.net.set_map("TINY.LVL");
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Join;
        cfg.net_address = "127.0.0.1";
        cfg.net_port = host.net.listen_port();
        cfg.player_name = "Bob";
        Application app;
        ASSERT_TRUE(app.init(cfg));
        VirtualClock clock;
        clock.set(app);
        Duo duo{app, host};
        ASSERT_TRUE(duo.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room; }, 8000));
        app.set_page_hidden(true);
        for (int i = 0; i < 51; ++i) {                                                    // 25 s in a room, a wake-up every half second
            clock.advance(0.5);
            host.now += 500;
            host.update();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            app.background_pump();
        }
        ASSERT_EQ(app.net()->phase(), net::NetGame::Phase::Room);
        app.set_page_hidden(false);
        const std::string line = app.last_hidden_line();
        ASSERT_TRUE(line.find("25.") != std::string::npos);
        ASSERT_TRUE(line.find("wake-ups kept the connection going") != std::string::npos);
        ASSERT_TRUE(line.find("advanced by 0 ticks") == std::string::npos);
        // a tab that is flicked through (258 times) says nothing, and a tab that is left for two seconds at a time says it now and then, not every time
        int lines = 0;
        for (int i = 0; i < 258; ++i) {
            app.set_page_hidden(true);
            clock.advance(0.08);
            app.background_pump();
            clock.advance(0.07);
            app.set_page_hidden(false);
            if (!app.last_hidden_line().empty()) ++lines;
        }
        ASSERT_EQ(lines, 0);
        for (int i = 0; i < 20; ++i) {                                                    // two seconds hidden, one second shown: a minute
            app.set_page_hidden(true);
            clock.advance(1.0);
            app.background_pump();
            clock.advance(1.0);
            app.set_page_hidden(false);
            if (!app.last_hidden_line().empty()) ++lines;
            clock.advance(1.0);
        }
        ASSERT_TRUE(lines >= 4 && lines <= 6);                                            // one at the most every ten seconds
        {   // each hidden period tells its own ticks and wake-ups, not the sums with the periods before it (a short one says nothing, but its ticks and wake-ups were there)
            Peer host2;
            Application app2;
            ASSERT_TRUE(start_two(host2, app2, 1717));
            VirtualClock clock2;
            clock2.set(app2);
            HiddenDuo hidden2{app2, host2};
            hidden2.clock = &clock2;
            Duo duo2{app2, host2};
            app2.set_page_hidden(true);
            hidden2.step(800);
            app2.set_page_hidden(false);
            ASSERT_TRUE(app2.last_hidden_line().empty());                                 // 0.8 s: nothing to say
            duo2.step(500);
            app2.set_page_hidden(true);
            const uint64_t tick_at_hide = app2.sim().current_tick();
            const uint32_t pumps_at_hide = app2.background_pumps();
            hidden2.step(2000);
            app2.set_page_hidden(false);
            const uint64_t ticks = app2.sim().current_tick() - tick_at_hide;
            ASSERT_TRUE(ticks >= 30);
            ASSERT_EQ(app2.last_hidden_line(), "The page was hidden for 2.0 s; the match advanced by " + std::to_string(ticks) + " ticks and " +
                                                   std::to_string(app2.background_pumps() - pumps_at_hide) + " wake-ups stepped it in the background");
        }
    } TEST_END();
}

// Bots that fill the empty seats at START and chat in the waiting room (protocol 11): the application's hooks (--fill-bots, --say), the leader of a server's room and the host of a room
// on the local network
void run_room_bot_tests() {
    const auto click_start = [](Application& app) {
        const int32_t x = MapSelectScreen::BTN_START_X + 5;
        const int32_t y = MapSelectScreen::BTN_START_Y + 5;
        app.map_select().handle_mouse_motion(x, y);
        app.map_select().handle_mouse_down(x, y, 1);
        app.map_select().handle_mouse_up(x, y, 1);
    };
    const auto join_config = [](const Server& server, const std::string& room, const std::string& name) {
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Join;
        cfg.net_address = "127.0.0.1";
        cfg.net_port = server.port();
        cfg.net_room = room;
        cfg.player_name = name;
        return cfg;
    };
    const auto lines = [](const std::vector<net::ChatLine>& v) {
        std::vector<std::string> out;
        for (const net::ChatLine& l : v) out.push_back(std::to_string(static_cast<unsigned>(l.seat)) + "|" + l.name + "|" + l.text);
        return out;
    };

    TEST_CASE("N5.45 Command Line: --fill-bots none|easy|medium|hard Is The Bots That This Player's START Seats In The Empty Seats Of Its Room (Any Case; Off By Default; Anything Else Or No Value Refuses To Start), --say TEXT Is A Test Hook That Says A Line In The Waiting Room") {
        std::vector<std::string> args;
        std::vector<char*> st;
        args = {"ants", "--join", "127.0.0.1:4001", "--room", "R-1"};
        ApplicationConfig c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_TRUE(c.fill_bots == net::FillLevel::None && c.startup_error.empty() && c.net_say.empty());          // off by default: the START of every earlier version
        const std::pair<const char*, net::FillLevel> good[] = {{"none", net::FillLevel::None}, {"easy", net::FillLevel::Easy}, {"medium", net::FillLevel::Medium}, {"hard", net::FillLevel::Hard},
                                                                {"HARD", net::FillLevel::Hard}, {"Medium", net::FillLevel::Medium}};
        for (const auto& g : good) {
            args = {"ants", "--fill-bots", g.first, "--host"};
            c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
            ASSERT_TRUE(c.fill_bots == g.second && c.startup_error.empty());
        }
        for (const char* bad : {"harder", "", "1", "easy medium", "none,easy"}) {
            args = {"ants", "--fill-bots", bad};
            c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
            ASSERT_TRUE(c.fill_bots == net::FillLevel::None && c.startup_error.find("--fill-bots") != std::string::npos);
            Application refuses;
            ASSERT_FALSE(refuses.init(c));                                                    // refused at startup, with the reason on stderr
        }
        args = {"ants", "--fill-bots"};                                                       // no value
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_TRUE(c.startup_error.find("--fill-bots") != std::string::npos);
        args = {"ants", "--say", "hello there", "--join", "127.0.0.1:4001"};
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_EQ(c.net_say, std::string("hello there"));
    } TEST_END();

    TEST_CASE("N5.46 Leader With --fill-bots And --say: The Line Is Said Once When Two Players Are In The Room (The Other Player Hears It With The Leader's Name, The Leader Hears The Reply), The Leader's START Seats Bots In The Two Empty Seats (The Leader Did Not Need A Third Person), The Match Runs With Four Teams And The Two Machines Stay Identical; The Lines Of The Waiting Room Are Kept") {
        Server server;
        ASSERT_TRUE(server.make_room("FILL-APP", 4));
        ApplicationConfig cfg = join_config(server, "FILL-APP", "Leader");
        cfg.fill_bots = net::FillLevel::Hard;
        cfg.net_say = "hello from the leader";
        Application app;
        ASSERT_TRUE(app.init(cfg));
        Peer bob;
        Hall hall{server, &app, {&bob}};
        ASSERT_TRUE(hall.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && app.net()->is_leader(); }, 8000));
        ASSERT_EQ(app.net()->fill_bots(), net::FillLevel::Hard);
        app.set_fill_bots(net::FillLevel::Easy);                                              // the screens' way: the player chooses, any time before START
        ASSERT_TRUE(app.fill_bots() == net::FillLevel::Easy && app.net()->fill_bots() == net::FillLevel::Easy);
        app.set_fill_bots(net::FillLevel::Hard);
        ASSERT_TRUE(app.fill_bots() == net::FillLevel::Hard && app.net()->fill_bots() == net::FillLevel::Hard);
        hall.step(1500);
        ASSERT_TRUE(app.net()->pregame_chat().empty());                                       // alone, the hook waits for somebody to hear it
        ASSERT_TRUE(bob.net.join("127.0.0.1", server.port(), "Bob", 255, "FILL-APP"));
        ASSERT_TRUE(hall.until([&]() { return !bob.net.pregame_chat().empty(); }, 8000));
        ASSERT_EQ(lines(bob.net.pregame_chat()), (std::vector<std::string>{"0|Leader|hello from the leader"}));
        ASSERT_EQ(lines(app.net()->pregame_chat()), lines(bob.net.pregame_chat()));          // the room tells the leader its own line as well
        hall.step(2000);
        ASSERT_EQ(bob.net.pregame_chat().size(), size_t{1});                                  // once, not again and again
        ASSERT_TRUE(bob.net.chat("hi leader"));
        ASSERT_TRUE(hall.until([&]() { return app.net()->pregame_chat().size() == 2; }, 8000));
        ASSERT_EQ(lines(app.net()->pregame_chat()).back(), std::string("1|Bob|hi leader"));
        ASSERT_TRUE(hall.until([&]() { return app.net()->room().slots[1].rtt_ms != net::kRttUnknown; }, 8000));
        click_start(app);                                                                     // the leader's START: the request carries the fill level
        ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 15000));
        const server::RoomStatus s = server.status("FILL-APP");
        ASSERT_TRUE(s.state == server::RoomState::Running && s.joined == 4 && s.expected == 4 && s.bots.size() == 2);
        ASSERT_TRUE(s.names[0] == "Leader" && s.names[1] == "Bob" && s.names[2] == "Bot (Hard)" && s.names[3] == "Bot (Hard)");
        ASSERT_TRUE(s.bots[0].seat == 2 && s.bots[0].level == "hard" && s.bots[0].fill && s.bots[1].seat == 3);
        ASSERT_EQ(app.sim().roster_mask(), 0x0F);
        ASSERT_EQ(bob.sim.roster_mask(), 0x0F);
        ASSERT_EQ(app.sim().get_player_name(3), "Bot (Hard)");                                // the screens name the bots as bots
        ASSERT_TRUE(app.bots() == nullptr);                                                   // a guest runs no bot: the server does
        hall.step(6000);
        ASSERT_TRUE(hall.identical(app.sim(), bob.sim));
        ASSERT_FALSE(app.net()->desynced() || bob.net.desynced());
        ASSERT_TRUE(server.status("FILL-APP").state == server::RoomState::Running);          // the referee agrees
        ASSERT_EQ(app.net()->pregame_chat().size(), size_t{2});                              // the waiting room's lines are still there for the match's log
        app.quit();
        ASSERT_FALSE(app.network_active());
    } TEST_END();

    TEST_CASE("N5.47 Leader Alone With --fill-bots: START Is Not The Can't-Go Cue Any More (One Person Is Enough): The Server Seats Three Bots And Starts; With Fog Of War In The Room The Leader's Status Line Says Bots Cannot Play With It And The Room Waits") {
        {
            Server server;
            ASSERT_TRUE(server.make_room("ALONE-APP", 4));
            ApplicationConfig cfg = join_config(server, "ALONE-APP", "Solo");
            cfg.fill_bots = net::FillLevel::Medium;
            Application app;
            ASSERT_TRUE(app.init(cfg));
            Hall hall{server, &app, {}};
            ASSERT_TRUE(hall.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && app.net()->is_leader(); }, 8000));
            const size_t channels = app.audio_mixer().active_channel_count();
            click_start(app);
            ASSERT_EQ(app.audio_mixer().active_channel_count(), channels + 1);               // the click only: no can't-go cue
            ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing; }, 15000));
            const server::RoomStatus s = server.status("ALONE-APP");
            ASSERT_TRUE(s.state == server::RoomState::Running && s.joined == 4 && s.bots.size() == 3 && s.names[0] == "Solo");
            ASSERT_EQ(app.sim().roster_mask(), 0x0F);
            hall.step(kDialogMs + 4000);                                                     // (the server seals the first turn kDialogMs after the match began: 4 s of play follow)
            ASSERT_FALSE(app.net()->desynced());
            ASSERT_TRUE(server.status("ALONE-APP").ticks > 40);
            app.quit();
        }
        {   // without a fill the same START is the can't-go cue, as before
            Server server;
            ASSERT_TRUE(server.make_room("ALONE-NOFILL", 4));
            Application app;
            ASSERT_TRUE(app.init(join_config(server, "ALONE-NOFILL", "Solo")));
            Hall hall{server, &app, {}};
            ASSERT_TRUE(hall.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && app.net()->is_leader(); }, 8000));
            const size_t channels = app.audio_mixer().active_channel_count();
            click_start(app);
            ASSERT_EQ(app.audio_mixer().active_channel_count(), channels + 2);               // the click and the cue
            hall.step(1000);
            ASSERT_TRUE(server.status("ALONE-NOFILL").state == server::RoomState::Waiting && server.status("ALONE-NOFILL").bots.empty());
        }
        {   // Fog of War: the server refuses the bots and says so; the room is not started
            Server server;
            server::RoomSpec spec;
            spec.code = "ALONE-FOG";
            spec.map = "TINY.LVL";
            spec.players = 4;
            spec.fog = true;
            ASSERT_TRUE(server.mgr.create_room(spec, server.now).ok);
            ApplicationConfig cfg = join_config(server, "ALONE-FOG", "Solo");
            cfg.fill_bots = net::FillLevel::Easy;
            Application app;
            ASSERT_TRUE(app.init(cfg));
            Hall hall{server, &app, {}};
            ASSERT_TRUE(hall.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && app.net()->is_leader(); }, 8000));
            click_start(app);
            ASSERT_TRUE(hall.until([&]() { return app.net()->status_text() == net::kNoticeFillFog; }, 8000));      // the notice of the room is on the status line
            ASSERT_EQ(app.map_select().room().status, std::string(net::kNoticeFillFog));
            ASSERT_TRUE(app.net()->pregame_chat().size() == 1 && app.net()->pregame_chat()[0].notice());
            hall.step(1500);
            ASSERT_TRUE(server.status("ALONE-FOG").state == server::RoomState::Waiting && server.status("ALONE-FOG").bots.empty());
            ASSERT_EQ(app.state(), AppState::MapSelect);
        }
    } TEST_END();

    TEST_CASE("N5.48 Host Of A Room On The Local Network With --fill-bots: START Seats Bots In The Empty Seats And This Machine Runs Them (The Controller Holds Exactly Those Seats); A Start That Is Cancelled Takes Them Out Again; Fog Of War Seats None") {
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Host;
        cfg.net_port = 0;
        cfg.net_loopback_only = true;
        cfg.player_name = "Alice";
        cfg.fill_bots = net::FillLevel::Easy;
        Application app;
        ASSERT_TRUE(app.init(cfg));
        {   // a guest that says Hello and never answers a ping: its thumb has not appeared, so START cannot go through (the can't-go cue), and the bots that the fill seated for it go again
            auto mute = net::TcpConnection::connect("127.0.0.1", app.net()->listen_port());
            ASSERT_TRUE(mute != nullptr);
            net::HelloMsg hello;
            hello.name = "Mute";
            bool sent = false;
            for (int i = 0; i < 800 && app.net()->room().slots[1].state != net::SlotState::Client; ++i) {
                app.pump_network(0.010f);
                std::vector<uint8_t> unread;
                mute->poll(unread);                                                          // (it reads the host's pings and never answers them)
                if (!sent && mute->is_open()) sent = mute->send(net::encode(hello));
                std::this_thread::sleep_for(std::chrono::microseconds(300));
            }
            ASSERT_TRUE(app.net()->room().slots[1].state == net::SlotState::Client && app.net()->room().slots[1].rtt_ms == net::kRttUnknown);
            const size_t channels = app.audio_mixer().active_channel_count();
            app.map_select().handle_key_down(SDLK_RETURN);
            ASSERT_EQ(app.audio_mixer().active_channel_count(), channels + 1);               // the cue (sound 63)
            ASSERT_TRUE(app.net()->room().slots[2].state == net::SlotState::Empty && app.net()->room().slots[3].state == net::SlotState::Empty);
            ASSERT_FALSE(app.map_select().is_locked());
            ASSERT_EQ(app.net()->phase(), net::NetGame::Phase::Room);
            mute->close();
            for (int i = 0; i < 800 && app.net()->room().slots[1].state != net::SlotState::Empty; ++i) {
                app.pump_network(0.010f);
                std::this_thread::sleep_for(std::chrono::microseconds(300));
            }
            ASSERT_TRUE(app.net()->room().slots[1].state == net::SlotState::Empty);
        }
        Peer bob;
        ASSERT_TRUE(bob.net.join("127.0.0.1", app.net()->listen_port(), "Bob"));
        Duo duo{app, bob};
        ASSERT_TRUE(duo.until([&]() { return app.net()->room().slots[1].state == net::SlotState::Client && app.net()->room().slots[1].rtt_ms != net::kRttUnknown; }, 8000));
        // Bob's machine cannot load the map: the start is cancelled and the bots that it seated go again
        bob.hold_report = true;
        app.map_select().handle_key_down(SDLK_RETURN);
        ASSERT_TRUE(duo.until([&]() { return bob.report_pending; }, 8000));
        ASSERT_TRUE(app.net()->room().slots[2].state == net::SlotState::Bot && app.net()->room().slots[3].state == net::SlotState::Bot);
        ASSERT_EQ(app.net()->room().slots[2].name, "Bot (Easy)");
        bob.report_pending = false;
        bob.net.report_loaded(false);
        ASSERT_TRUE(duo.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && !app.map_select().is_locked(); }, 8000));
        ASSERT_TRUE(app.net()->room().slots[2].state == net::SlotState::Empty && app.net()->room().slots[3].state == net::SlotState::Empty);
        ASSERT_TRUE(duo.until([&]() { return bob.net.room().slots[2].state == net::SlotState::Empty; }, 3000));                  // Bob sees the room without them
        // the same START with a machine that loads: two people and two bots, the host's machine runs the bots
        bob.hold_report = false;
        duo.step(500);
        app.map_select().handle_key_down(SDLK_RETURN);
        ASSERT_TRUE(duo.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 8000));
        ASSERT_EQ(app.sim().roster_mask(), 0x0F);
        ASSERT_EQ(bob.sim.roster_mask(), 0x0F);
        ASSERT_TRUE(app.bots() != nullptr);
        ASSERT_EQ(app.bots()->seat_mask(), 0x0C);                                            // exactly the seats that the fill took: 2 and 3
        ASSERT_TRUE(app.sim().get_player_name(2) == "Bot (Easy)" && app.sim().get_player_name(3) == "Bot (Easy)");
        duo.step(8000);
        ASSERT_TRUE(app.bots()->stats(2).decisions > 0 && app.bots()->stats(3).decisions > 0);
        ASSERT_EQ(app.bots()->stats(2).rejected, 0u);
        bool same = false;                                                                   // both machines stand at the same tick at some moment: their states are then equal
        for (int i = 0; i < 400 && !same; ++i) {
            if (app.sim().current_tick() == bob.sim.current_tick()) {
                ASSERT_TRUE(app.sim().state_hash() == bob.sim.state_hash());
                same = true;
            }
            duo.step(10);
        }
        ASSERT_TRUE(same);
        ASSERT_FALSE(app.net()->desynced() || bob.net.desynced());
        app.quit();
    } TEST_END();
    TEST_CASE("N5.77 Command Line: --fill-bots Takes One Word (Every Seat) Or Four Joined By Commas (The Seats 0 To 3: A Level For Each Seat, Protocol 13), Any Case; Anything Else Is A Startup Error That Names The Option And Says What Is Wrong; The Application's Setters Take A Plan Or One Level") {
        std::vector<std::string> args;
        std::vector<char*> st;
        const auto parsed = [&](std::vector<std::string> a) {
            args = std::move(a);
            return Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        };
        using L = net::FillLevel;
        const std::pair<const char*, net::FillPlan> good[] = {
            {"easy", net::FillPlan(L::Easy)},
            {"none,none,easy,hard", net::FillPlan(std::array<L, 4>{L::None, L::None, L::Easy, L::Hard})},
            {"NONE,Easy,MEDIUM,hard", net::FillPlan(std::array<L, 4>{L::None, L::Easy, L::Medium, L::Hard})},
            {"hard,hard,hard,hard", net::FillPlan(L::Hard)},
            {"none,none,none,none", net::FillPlan()},
        };
        for (const auto& g : good) {
            const ApplicationConfig c = parsed({"ants", "--host", "--fill-bots", g.first});
            ASSERT_TRUE(c.startup_error.empty() && c.fill_bots == g.second);
        }
        ASSERT_TRUE(parsed({"ants", "--fill-bots", "none,none,easy,hard", "--fill-bots", "medium", "--host"}).fill_bots == net::FillPlan(L::Medium));        // the last one wins
        for (const char* bad : {"none,easy", "none,none,easy", "none,none,easy,hard,hard", "none,,easy,hard", "none, none, easy, hard", ",none,easy,hard", "easy,", "none;none;easy;hard", "harder,easy,easy,easy", "1,2,3,4"}) {
            const ApplicationConfig c = parsed({"ants", "--host", "--fill-bots", bad});
            ASSERT_TRUE(c.fill_bots == net::FillPlan() && c.startup_error.find(std::string("--fill-bots ") + bad + ": ") == 0 && c.startup_error.size() > std::string("--fill-bots ") .size() + std::string(bad).size() + 4);
            Application refuses;
            ASSERT_FALSE(refuses.init(c));
        }
        const ApplicationConfig missing = parsed({"ants", "--fill-bots"});
        ASSERT_TRUE(missing.startup_error.find("--fill-bots needs none, easy, medium or hard") == 0 && missing.startup_error.find("four") != std::string::npos);
        {   // the setters: a plan, or one level (every seat); the NetGame has it
            ApplicationConfig cfg = headless_config();
            cfg.net_role = ApplicationConfig::NetRole::Host;
            cfg.net_port = 0;
            cfg.net_loopback_only = true;
            cfg.player_name = "Alice";
            cfg.fill_bots = net::FillPlan(std::array<L, 4>{L::None, L::None, L::Easy, L::Hard});
            cfg.teams = LocalTeams{true, 0, 2};
            Application app;
            ASSERT_TRUE(app.init(cfg));
            ASSERT_TRUE(app.net()->fill_bots() == cfg.fill_bots && app.net()->start_teams() == LocalTeams({true, 0, 2}));      // the machine's START carries both choices
            app.set_fill_bots(L::Medium);
            ASSERT_TRUE(app.fill_bots() == net::FillPlan(L::Medium) && app.net()->fill_bots() == net::FillPlan(L::Medium));
            app.set_fill_bots(net::FillPlan(std::array<L, 4>{L::None, L::Hard, L::None, L::None}));
            ASSERT_TRUE(app.fill_bots() == net::FillPlan(std::array<L, 4>{L::None, L::Hard, L::None, L::None}) && app.net()->fill_bots() == app.fill_bots());
            app.set_start_teams(LocalTeams{true, 1, 2});
            ASSERT_TRUE(app.start_teams() == LocalTeams({true, 1, 2}) && app.net()->start_teams() == LocalTeams({true, 1, 2}));
            app.set_start_teams(LocalTeams{});
            ASSERT_TRUE(!app.start_teams().set && !app.net()->start_teams().set);
            app.quit();
        }
    } TEST_END();

    TEST_CASE("N5.78 Protocol 13, A Server's Room With Two Games, Two Bots Of Their Own Levels And The Leader's Teams: The Prompt And The Footer Say The Plan; Every Engine (The Referee's, Both Machines') Has The Alliances Before Its First Tick, Each Game's Chat Log Has The News Flash Once For Each Pair, The Hashes Agree, Nobody Is Told That Teams Could Not Be Made") {
        using L = net::FillLevel;
        Server server;
        ASSERT_TRUE(server.make_room("TEAMS-APP", 4));
        ApplicationConfig lead_cfg = join_config(server, "TEAMS-APP", "Leader");
        lead_cfg.fill_bots = net::FillPlan(std::array<L, 4>{L::None, L::None, L::Easy, L::Hard});       // Blue gets an Easy bot, Black a Hard bot (seat 1 is the guest's)
        lead_cfg.teams = LocalTeams{true, 0, 2};                                                         // Green + Blue against Red + Black: the leader and the Easy bot against the guest and the Hard bot
        Application leader;
        ASSERT_TRUE(leader.init(lead_cfg));
        Hall hall{server, &leader, {}};
        ASSERT_TRUE(hall.until([&]() { return leader.net()->phase() == net::NetGame::Phase::Room && leader.net()->is_leader(); }, 8000));
        Application guest;
        ASSERT_TRUE(guest.init(join_config(server, "TEAMS-APP", "Guest")));
        hall.second = &guest;
        ASSERT_TRUE(hall.until([&]() { return guest.net()->phase() == net::NetGame::Phase::Room && leader.net()->room().slots[1].state == net::SlotState::Client && leader.net()->room().slots[1].rtt_ms != net::kRttUnknown; }, 8000));
        hall.step(5300);                                                                                  // (the first five seconds of company say how to change a colour: the plan's line is back after them)
        // what the leader is told before START: the longest way of saying it that fits the label
        const std::vector<std::string>& prompts = leader.net()->prompt_texts();
        ASSERT_TRUE(prompts.size() >= 4 && prompts.front() == "Press START: Blue gets an Easy bot, Black a Hard bot; teams Green + Blue against Red + Black.");
        const std::string shown = leader.map_select().room().status;
        ASSERT_TRUE(std::find(prompts.begin(), prompts.end(), shown) != prompts.end() && leader.map_select().prompt_fits(leader.renderer(), shown));
        for (const std::string& text : prompts) {
            if (text == shown) break;
            ASSERT_FALSE(leader.map_select().prompt_fits(leader.renderer(), text));                      // (every longer way did not fit: this is the longest that does)
        }
        ASSERT_TRUE(shown.find("Blue") != std::string::npos && shown.find("Black") != std::string::npos && shown.find("teams") != std::string::npos);
        ASSERT_EQ(guest.map_select().room().status, std::string(sim::strings::text(sim::strings::kWaitingForHost)));      // a guest keeps its own line
        click_start(leader);
        ASSERT_TRUE(hall.until([&]() { return leader.state() == AppState::Playing && guest.state() == AppState::Playing; }, 15000));
        server::RoomStatus s = server.status("TEAMS-APP");
        ASSERT_TRUE(s.state == server::RoomState::Running && s.joined == 4 && s.bots.size() == 2);
        ASSERT_TRUE(s.bots[0].seat == 2 && s.bots[0].level == "easy" && s.bots[0].name == "Bot (Easy)" && s.bots[1].seat == 3 && s.bots[1].level == "hard" && s.bots[1].name == "Bot (Hard)");
        ASSERT_EQ(s.teams, std::string("0+2"));
        ASSERT_TRUE(s.allies[0] == 2 && s.allies[1] == 3 && s.allies[2] == 0 && s.allies[3] == 1);        // the referee's engine, in the dialog: tick 0
        ASSERT_EQ(s.ticks, 0u);
        for (Application* a : {&leader, &guest}) {                                                        // both games: made on the Start, before any tick
            ASSERT_EQ(a->sim().current_tick(), uint64_t{0});
            for (uint8_t seat = 0; seat < 4; ++seat) ASSERT_EQ(a->sim().alliance_of(seat), static_cast<uint8_t>(seat == 0 ? 2 : seat == 1 ? 3 : seat == 2 ? 0 : 1));
            ASSERT_EQ(a->sim().get_world_state().pending_invite_from[0], 255);                           // no invitation is left open (no dialog)
        }
        ASSERT_EQ(leader.net()->start_info().team_a, 0);
        ASSERT_EQ(guest.net()->start_info().team_b, 2);
        hall.step(kDialogMs + 1500);                                                                     // the match runs: the first tick, the News Flash
        const auto count_of = [](const std::string& text, const std::string& what) {
            size_t n = 0;
            for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + what.size())) ++n;
            return n;
        };
        for (Application* a : {&leader, &guest}) {
            const std::string log = a->hud().chat_transcript("t");
            ASSERT_EQ(count_of(log, "are a team now!"), size_t{2});                                       // once for each pair, on each machine
            ASSERT_TRUE(log.find("Leader (Green) and Bot (Easy) (Blue) are a team now!") != std::string::npos);
            ASSERT_TRUE(log.find("Guest (Red) and Bot (Hard) (Black) are a team now!") != std::string::npos);
            ASSERT_TRUE(a->net()->pregame_chat().empty() || !a->net()->pregame_chat().back().notice());      // nobody was told that teams could not be made
        }
        ASSERT_TRUE(hall.identical(leader.sim(), guest.sim()));
        ASSERT_FALSE(leader.net()->desynced() || guest.net()->desynced());
        hall.step(3000);
        s = server.status("TEAMS-APP");
        ASSERT_TRUE(s.state == server::RoomState::Running && s.ticks > 60 && s.allies[0] == 2 && s.allies[3] == 1);        // (the referee agrees to the hash of every 20th turn: a room that disagreed would have failed)
        ASSERT_TRUE(leader.bots() == nullptr && guest.bots() == nullptr);                                // the server runs the bots
        leader.quit();
        guest.quit();
    } TEST_END();

    TEST_CASE("N5.79 Protocol 13, The Host Of A Room On The Local Network: START Seats The Bots Of Their Own Levels (This Machine Runs Them: The Controller Holds Those Seats With Those Levels) And Puts The Host's Teams Into The Start; A Guest's Engine Has The Alliances At Tick 0 And Stands At The Host's State; Teams That Cannot Be Made Say Why On The Host's Status Line And In A Notice To The Guest") {
        using L = net::FillLevel;
        {
            ApplicationConfig cfg = headless_config();
            cfg.net_role = ApplicationConfig::NetRole::Host;
            cfg.net_port = 0;
            cfg.net_loopback_only = true;
            cfg.player_name = "Alice";
            cfg.fill_bots = net::FillPlan(std::array<L, 4>{L::None, L::None, L::Easy, L::Hard});
            cfg.teams = LocalTeams{true, 0, 2};
            Application app;
            ASSERT_TRUE(app.init(cfg));
            Peer bob;
            ASSERT_TRUE(bob.net.join("127.0.0.1", app.net()->listen_port(), "Bob"));
            Duo duo{app, bob};
            ASSERT_TRUE(duo.until([&]() { return app.net()->room().slots[1].state == net::SlotState::Client && app.net()->room().slots[1].rtt_ms != net::kRttUnknown; }, 8000));
            duo.step(200);
            ASSERT_EQ(app.map_select().room().status.find("Blue gets an Easy bot, Black a Hard bot") == std::string::npos, false);        // the host's prompt says the plan
            app.map_select().handle_key_down(SDLK_RETURN);
            ASSERT_TRUE(duo.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 8000));
            ASSERT_EQ(app.sim().roster_mask(), 0x0F);
            ASSERT_TRUE(app.bots() != nullptr);
            ASSERT_EQ(app.bots()->seat_mask(), 0x0C);                                                  // exactly seats 2 and 3, each with its own level
            ASSERT_TRUE(app.bots()->profile(2) != nullptr && app.bots()->profile(3) != nullptr);
            ASSERT_EQ(app.bots()->profile(2)->rate_milli_cps, ai::profile_for(ai::Level::Easy).rate_milli_cps);
            ASSERT_EQ(app.bots()->profile(3)->rate_milli_cps, ai::profile_for(ai::Level::Hard).rate_milli_cps);
            ASSERT_TRUE(ai::profile_for(ai::Level::Easy).rate_milli_cps != ai::profile_for(ai::Level::Hard).rate_milli_cps);
            ASSERT_TRUE(app.sim().get_player_name(2) == "Bot (Easy)" && app.sim().get_player_name(3) == "Bot (Hard)");
            ASSERT_EQ(app.net()->start_info().team_a, 0);
            ASSERT_EQ(bob.net.start_info().team_b, 2);
            ASSERT_EQ(app.sim().current_tick(), uint64_t{0});
            for (uint8_t seat = 0; seat < 4; ++seat) {
                const uint8_t want = static_cast<uint8_t>(seat == 0 ? 2 : seat == 1 ? 3 : seat == 2 ? 0 : 1);
                ASSERT_TRUE(app.sim().alliance_of(seat) == want && bob.sim.alliance_of(seat) == want);
            }
            duo.step(kDialogMs + 3000);
            const std::string log = app.hud().chat_transcript("t");
            size_t told = 0;
            for (size_t at = log.find("are a team now!"); at != std::string::npos; at = log.find("are a team now!", at + 1)) ++told;
            ASSERT_EQ(told, size_t{2});
            ASSERT_TRUE(app.bots()->stats(2).decisions > 0 && app.bots()->stats(3).decisions > 0);
            ASSERT_TRUE(duo.until([&]() { return app.sim().current_tick() == bob.sim.current_tick(); }, 4000));
            ASSERT_TRUE(app.sim().state_hash() == bob.sim.state_hash());
            ASSERT_FALSE(app.net()->desynced() || bob.net.desynced());
            for (uint8_t seat = 0; seat < 4; ++seat) ASSERT_EQ(app.sim().alliance_of(seat), static_cast<uint8_t>(seat == 0 ? 2 : seat == 1 ? 3 : seat == 2 ? 0 : 1));       // (the standard bots never broke a team)
            app.quit();
        }
        {   // teams that cannot be made: two people play (the host and Bob, no bots), the team would be both of them
            ApplicationConfig cfg = headless_config();
            cfg.net_role = ApplicationConfig::NetRole::Host;
            cfg.net_port = 0;
            cfg.net_loopback_only = true;
            cfg.player_name = "Alice";
            cfg.teams = LocalTeams{true, 0, 1};
            Application app;
            ASSERT_TRUE(app.init(cfg));
            Peer bob;
            ASSERT_TRUE(bob.net.join("127.0.0.1", app.net()->listen_port(), "Bob"));
            Duo duo{app, bob};
            ASSERT_TRUE(duo.until([&]() { return app.net()->room().slots[1].state == net::SlotState::Client && app.net()->room().slots[1].rtt_ms != net::kRttUnknown; }, 8000));
            duo.step(200);
            ASSERT_TRUE(app.map_select().room().status.find("teams Green + Red") != std::string::npos);                                // the prompt says the choice
            app.map_select().handle_key_down(SDLK_RETURN);
            const std::string text = std::string(net::kNoticeNoTeams) + "only two seats play: a team of them would end the match at once.";
            ASSERT_EQ(app.net()->status_text(), text);
            ASSERT_TRUE(duo.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing && !bob.net.pregame_chat().empty(); }, 8000));
            ASSERT_TRUE(bob.net.pregame_chat().size() == 1 && bob.net.pregame_chat()[0].notice() && bob.net.pregame_chat()[0].text == text);
            for (uint8_t seat = 0; seat < 4; ++seat) ASSERT_TRUE(app.sim().alliance_of(seat) == sim::ALLIANCE_NONE && bob.sim.alliance_of(seat) == sim::ALLIANCE_NONE);
            ASSERT_EQ(app.net()->start_info().team_a, net::kNoTeam);
            app.quit();
        }
    } TEST_END();

    TEST_CASE("N5.80 Protocol 13, The Texts Fit Their Places With The Real Font: The Leader's Prompt Has A Way Of Being Said That Fits The Two Lines Of The Status Label On The Classic Page (293 px) And On The 16:9 Page (363 px) For Every Plan And Every Teams Choice; Every Line Of The Footer Fits The Players' Status Box At 12 px At The Latest (18 px For The Footer Of Protocol 11)") {
        using L = net::FillLevel;
        ApplicationConfig cfg = headless_config();
        Application app;
        ASSERT_TRUE(app.init(cfg));
        const Renderer& r = app.renderer();
        std::vector<net::FillPlan> plans = {net::FillPlan(), net::FillPlan(L::Easy), net::FillPlan(L::Medium), net::FillPlan(L::Hard)};
        for (const std::array<L, 3>& abc : {std::array<L, 3>{L::None, L::None, L::Hard}, std::array<L, 3>{L::Easy, L::None, L::Hard}, std::array<L, 3>{L::Easy, L::Hard, L::Medium},
                                            std::array<L, 3>{L::Medium, L::Medium, L::Medium}, std::array<L, 3>{L::Hard, L::Easy, L::Medium}, std::array<L, 3>{L::None, L::Medium, L::Easy},
                                            std::array<L, 3>{L::Medium, L::None, L::None}, std::array<L, 3>{L::None, L::Hard, L::None}, std::array<L, 3>{L::Easy, L::Easy, L::Hard},
                                            std::array<L, 3>{L::Hard, L::Medium, L::Hard}}) {
            plans.push_back(net::FillPlan(std::array<L, 4>{L::None, abc[0], abc[1], abc[2]}));        // (the host's own seat has no level)
        }
        std::vector<sim::StartTeams> teams = {sim::StartTeams{}};
        for (uint8_t a = 0; a < 4; ++a) {
            for (uint8_t b = 0; b < 4; ++b) {
                if (a != b) teams.push_back(sim::StartTeams{true, a, b});
            }
        }
        const std::vector<std::vector<uint8_t>> rooms = {{0}, {0, 2}, {1, 2, 3}, {0, 1, 2, 3}};
        using MS = MapSelectScreen;
        size_t checked = 0;
        size_t widest_classic = 0;
        size_t fitting = 0;
        size_t too_long = 0;
        int32_t widest_footer = 0;
        for (const net::FillPlan& plan : plans) {
            for (const sim::StartTeams& t : teams) {
                for (const std::vector<uint8_t>& people : rooms) {
                    for (const bool fog : {false, true}) {
                        net::RoomMsg room;
                        for (const uint8_t seat : people) room.slots[seat] = {net::SlotState::Client, "P", 10};
                      for (const bool room_owned : {false, true}) {                                 // (the teams are this START's own choice, or the room's: its code names them)
                        const std::vector<std::string> prompts = net::NetGame::start_prompt_texts(plan, t, room, fog, room_owned);
                        if (!prompts.empty()) {
                            // the shortest way of saying it fits the two lines of the classic label (293 px) and of the wide one (363 px); the longest that fits is what a screen picks
                            ASSERT_TRUE(wrap_label_text(r, prompts.back(), 293, FontSize::Px14).size() <= 2);
                            ASSERT_TRUE(wrap_label_text(r, prompts.back(), 363, FontSize::Px14).size() <= 2);
                            widest_classic = std::max(widest_classic, wrap_label_text(r, prompts.back(), 293, FontSize::Px14).size());
                            for (size_t i = 1; i < prompts.size(); ++i) ASSERT_TRUE(prompts[i].size() <= prompts[i - 1].size());
                            for (const std::string& text : prompts) {                              // the screen's rule is the label's own measure: two lines of 293 px on this (the classic) page
                                const bool two_lines = wrap_label_text(r, text, 293, FontSize::Px14).size() <= 2;
                                ASSERT_EQ(app.map_select().prompt_fits(r, text), two_lines);
                                (two_lines ? fitting : too_long) += 1;
                            }
                        }
                        const net::NetGame::FooterTexts footer = net::NetGame::start_footer(plan, t, room, fog, room_owned);
                        for (const std::vector<std::string>& line : footer.line) {
                            if (line.empty()) continue;
                            bool fits = false;
                            for (const std::string& text : line) {
                                const int32_t w = r.get_text_width(text, FontSize::Px12);
                                widest_footer = std::max(widest_footer, w);
                                fits = fits || w <= MS::footer_width();
                            }
                            ASSERT_TRUE(fits);                                                      // some way of saying the line fits the box at 12 px
                        }
                        ++checked;
                      }
                    }
                }
            }
        }
        ASSERT_TRUE(checked > 1000 && widest_footer > 0);
        ASSERT_TRUE(fitting > 1000 && too_long > 20);                                                // (the rule above met both answers: the long ways of a plan of three bots and teams do not fit)
        ASSERT_EQ(widest_classic, size_t{2});                                                        // (the shortest ways take the two lines that the label has, and no more)
        // the footer of protocol 11 is at 18 px: its lines fit the box at the size that the screen has always used
        ASSERT_EQ(MS::footer_font(r, "Empty seats at START:"), FontSize::Px18);
        ASSERT_EQ(MS::footer_font(r, "Medium bots"), FontSize::Px18);
        ASSERT_EQ(MS::footer_font(r, "Blue Easy, Black Hard"), FontSize::Px18);
        ASSERT_TRUE(MS::footer_font(r, "Teams: Green + Red against Blue + Black") != FontSize::Px18);
        ASSERT_EQ(MS::footer_font(r, std::string(80, 'W')), FontSize::Px12);                         // (what fits at no size is cut at 12 px by the screen)
        app.quit();
    } TEST_END();

    TEST_CASE("N5.81 Protocol 13, The Room's Own Teams In The Application (Its Code Names Them): A Leader Is Told That They Are The Room's And Its Own --teams Is Not What The Room Makes (The Referee's, Both Games' Engines Have The Room's Alliances Before Their First Tick, The News Flash Comes Once For Each Pair); A Room Of Three Fills Up With Two Games And A Bare Machine And Starts By Itself With The Room's Teams On Every Engine, Nobody Having Pressed START") {
        using L = net::FillLevel;
        server::ServerLimits limits;
        limits.demo_rooms = 4;
        limits.demo_map = "TINY.LVL";
        limits.demo_maps = {"TINY.LVL"};
        const auto count_of = [](const std::string& text, const std::string& what) {
            size_t n = 0;
            for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + what.size())) ++n;
            return n;
        };
        {   // a room of four whose code names Green + Red; the leader's own choice (--teams) is Red + Blue, and its START seats two bots
            Server server(limits);
            const std::string code = "demo-tiny-4p-t01-abcdef";
            ApplicationConfig lead_cfg = join_config(server, code, "Leader");
            lead_cfg.fill_bots = net::FillPlan(std::array<L, 4>{L::None, L::None, L::Easy, L::Hard});
            lead_cfg.teams = LocalTeams{true, 1, 2};
            Application leader;
            ASSERT_TRUE(leader.init(lead_cfg));
            Hall hall{server, &leader, {}};
            ASSERT_TRUE(hall.until([&]() { return leader.net()->phase() == net::NetGame::Phase::Room && leader.net()->is_leader(); }, 8000));
            Application guest;
            ASSERT_TRUE(guest.init(join_config(server, code, "Guest")));
            hall.second = &guest;
            ASSERT_TRUE(hall.until([&]() { return guest.net()->phase() == net::NetGame::Phase::Room && leader.net()->room().slots[1].state == net::SlotState::Client && leader.net()->room().slots[1].rtt_ms != net::kRttUnknown; }, 8000));
            hall.step(5300);                                                                              // (the first five seconds of company say how to change a colour: the plan's line is back after them)
            ASSERT_TRUE(server.status(code).room_teams == "0+1" && server.status(code).state == server::RoomState::Waiting);
            ASSERT_TRUE(leader.net()->room_teams() == sim::StartTeams({true, 0, 1}) && leader.net()->effective_teams() == sim::StartTeams({true, 0, 1}));
            ASSERT_TRUE(leader.net()->start_teams() == sim::StartTeams({true, 1, 2}));                    // (its own choice is kept, and is not what the screens show or the room makes)
            ASSERT_FALSE(guest.net()->room_teams().set && guest.net()->is_leader());
            const std::vector<std::string>& prompts = leader.net()->prompt_texts();
            ASSERT_TRUE(prompts.size() >= 4 && prompts.front() == "Press START: Blue gets an Easy bot, Black a Hard bot; the room's teams: Green + Red against Blue + Black.");
            for (const std::string& text : prompts) ASSERT_TRUE(text.find("Red + Blue") == std::string::npos);        // (the leader's own pair appears nowhere)
            const std::string shown = leader.map_select().room().status;
            ASSERT_TRUE(std::find(prompts.begin(), prompts.end(), shown) != prompts.end() && leader.map_select().prompt_fits(leader.renderer(), shown));
            ASSERT_TRUE(shown.find("Green + Red") != std::string::npos && shown.find("teams") != std::string::npos);
            click_start(leader);
            ASSERT_TRUE(hall.until([&]() { return leader.state() == AppState::Playing && guest.state() == AppState::Playing; }, 15000));
            server::RoomStatus s = server.status(code);
            ASSERT_TRUE(s.state == server::RoomState::Running && s.joined == 4 && s.bots.size() == 2 && s.teams == "0+1" && s.room_teams == "0+1");
            ASSERT_TRUE(s.allies[0] == 1 && s.allies[1] == 0 && s.allies[2] == 3 && s.allies[3] == 2);   // the referee: the room's pair, and the two bots as the other team (not Red + Blue)
            ASSERT_EQ(s.ticks, 0u);
            for (Application* a : {&leader, &guest}) {
                ASSERT_EQ(a->sim().current_tick(), uint64_t{0});
                for (uint8_t seat = 0; seat < 4; ++seat) ASSERT_EQ(a->sim().alliance_of(seat), static_cast<uint8_t>(seat ^ 1u));
            }
            ASSERT_TRUE(leader.net()->start_info().team_a == 0 && leader.net()->start_info().team_b == 1 && guest.net()->start_info().team_a == 0);
            hall.step(kDialogMs + 1500);
            for (Application* a : {&leader, &guest}) {
                const std::string log = a->hud().chat_transcript("t");
                ASSERT_EQ(count_of(log, "are a team now!"), size_t{2});
                ASSERT_TRUE(log.find("Leader (Green) and Guest (Red) are a team now!") != std::string::npos);
                ASSERT_TRUE(log.find("Bot (Easy) (Blue) and Bot (Hard) (Black) are a team now!") != std::string::npos);
                ASSERT_TRUE(a->net()->pregame_chat().empty() || !a->net()->pregame_chat().back().notice());
            }
            ASSERT_TRUE(hall.identical(leader.sim(), guest.sim()));
            ASSERT_FALSE(leader.net()->desynced() || guest.net()->desynced());
            hall.step(3000);
            s = server.status(code);
            ASSERT_TRUE(s.state == server::RoomState::Running && s.ticks > 60 && s.allies[0] == 1 && s.allies[3] == 2);        // (a room whose referee disagreed on a hash would have failed)
            leader.quit();
            guest.quit();
        }
        {   // a room of three whose code names Red + Blue (1 + 2): two games and a bare machine fill it, nobody presses START: it starts by itself with the room's teams, seat 0 plays alone
            Server server(limits);
            const std::string code = "demo-tiny-3p-t12-abcdef";
            ApplicationConfig lead_cfg = join_config(server, code, "Leader");
            lead_cfg.teams = LocalTeams{true, 0, 1};                                                    // (the leader's own choice: not the room's, and nobody asks the room for it)
            Application leader;
            ASSERT_TRUE(leader.init(lead_cfg));
            Peer carl;
            Hall hall{server, &leader, {&carl}};
            ASSERT_TRUE(hall.until([&]() { return leader.net()->phase() == net::NetGame::Phase::Room && leader.net()->is_leader(); }, 8000));
            Application guest;
            ASSERT_TRUE(guest.init(join_config(server, code, "Guest")));
            hall.second = &guest;
            ASSERT_TRUE(hall.until([&]() { return guest.net()->phase() == net::NetGame::Phase::Room; }, 8000));
            ASSERT_TRUE(server.status(code).state == server::RoomState::Waiting && server.status(code).joined == 2);
            ASSERT_TRUE(carl.net.join("127.0.0.1", server.port(), "Carl", 255, code));                    // the third seat: the room is full
            ASSERT_TRUE(hall.until([&]() { return leader.state() == AppState::Playing && guest.state() == AppState::Playing && carl.net.phase() == net::NetGame::Phase::Playing; }, 20000));
            server::RoomStatus s = server.status(code);
            ASSERT_TRUE(s.state == server::RoomState::Running && s.joined == 3 && s.bots.empty() && s.teams == "1+2" && s.room_teams == "1+2");
            ASSERT_TRUE(s.allies[0] == sim::ALLIANCE_NONE && s.allies[1] == 2 && s.allies[2] == 1 && s.allies[3] == sim::ALLIANCE_NONE);
            ASSERT_EQ(s.ticks, 0u);
            for (const sim::SimulationEngine* e : {&leader.sim(), &guest.sim(), &carl.sim}) {            // both games and the bare machine
                ASSERT_EQ(e->current_tick(), uint64_t{0});
                ASSERT_TRUE(e->alliance_of(0) == sim::ALLIANCE_NONE && e->alliance_of(1) == 2 && e->alliance_of(2) == 1 && e->alliance_of(3) == sim::ALLIANCE_NONE);
            }
            ASSERT_TRUE(leader.net()->start_info().team_a == 1 && leader.net()->start_info().team_b == 2);
            hall.step(kDialogMs + 1500);
            for (Application* a : {&leader, &guest}) {
                const std::string log = a->hud().chat_transcript("t");
                ASSERT_EQ(count_of(log, "are a team now!"), size_t{1});                                   // the one pair, on each game
                ASSERT_TRUE(log.find("Guest (Red) and Carl (Blue) are a team now!") != std::string::npos);
                ASSERT_TRUE(a->net()->pregame_chat().empty() || !a->net()->pregame_chat().back().notice());
            }
            ASSERT_TRUE(hall.identical(leader.sim(), guest.sim()));
            ASSERT_FALSE(leader.net()->desynced() || guest.net()->desynced() || carl.net.desynced());
            hall.step(3000);
            ASSERT_TRUE(server.status(code).state == server::RoomState::Running && server.status(code).ticks > 60);
            leader.quit();
            guest.quit();
        }
    } TEST_END();
}

// The chat input of a room's setup screen and what the screens say about START (protocol 11): T or a click on the status line opens the input, the focus rule, the match's chat log
// that starts with the waiting room's lines, the status line's prompt for a START that seats bots
void run_room_chat_ui_tests() {
    const auto join_config = [](const Server& server, const std::string& room, const std::string& name) {
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Join;
        cfg.net_address = "127.0.0.1";
        cfg.net_port = server.port();
        cfg.net_room = room;
        cfg.player_name = name;
        return cfg;
    };
    const auto lines = [](const std::vector<net::ChatLine>& v) {
        std::vector<std::string> out;
        for (const net::ChatLine& l : v) out.push_back(std::to_string(static_cast<unsigned>(l.seat)) + "|" + l.name + "|" + l.text);
        return out;
    };
    // the leader, and a second player who has joined and been measured: START works
    struct Duo2 {
        Server server;
        Application app;
        Peer bob;
        Hall hall{server, &app, {&bob}};
    };

    TEST_CASE("N5.49 The Chat Input Of A Room's Setup Screen: T Or A Click On The Status Line Opens It (The T Is Not Typed), Enter Sends The Line To The Room And Closes It, Esc Closes It; While It Is Open S, Q, X, Enter And The Arrows Do Nothing On The Screen (A Line Is Not A START Or A Leave), The Status Line Shows What Is Typed; Afterwards The Screen's Keys Work Again; Ctrl+T, A Held T And A Local Game's Screen Do Not Open It") {
        Duo2 d;
        ASSERT_TRUE(d.server.make_room("CHATUI-1", 4));
        ASSERT_TRUE(d.app.init(join_config(d.server, "CHATUI-1", "Leader")));
        ASSERT_TRUE(d.hall.until([&]() { return d.app.net()->phase() == net::NetGame::Phase::Room && d.app.net()->is_leader(); }, 8000));
        ASSERT_TRUE(d.bob.net.join("127.0.0.1", d.server.port(), "Bob", 255, "CHATUI-1"));
        ASSERT_TRUE(d.hall.until([&]() { return d.bob.net.phase() == net::NetGame::Phase::Room && d.app.net()->room().slots[1].rtt_ms != net::kRttUnknown; }, 8000));
        d.hall.step(700);                                                                     // (the setup screen has shown its labels: the refresh of 500 ms)
        Application& app = d.app;
        ASSERT_TRUE(app.room_chat_available() && !app.room_chat().is_open());
        // the keys that must not open it
        app.room_key_down(SDLK_t, KMOD_CTRL, false);
        app.room_key_down(SDLK_t, KMOD_GUI, false);
        app.room_key_down(SDLK_t, 0, true);                                                   // a held key's repeat
        ASSERT_FALSE(app.room_chat().is_open());
        // T opens it, and the text event of the same key press is not the first letter
        app.room_key_down(SDLK_t, 0, false);
        ASSERT_TRUE(app.room_chat().is_open());
        app.room_text_input("t");
        ASSERT_EQ(app.room_chat().text(), std::string());
        app.room_text_input("hello bob");
        ASSERT_EQ(app.room_chat().text(), std::string("hello bob"));
        d.hall.step(30);
        ASSERT_TRUE(app.map_select().room().status_input && app.map_select().room().status_prefix == "Say: " && app.map_select().room().status == "hello bob");   // the status line shows the line, typed (the screen draws the prompt, the end and a caret that blinks)
        // the screen's keys do nothing while the line is typed: not START (S), not Leave (Q, X), not the map (the arrows), and they are not typed either
        for (const SDL_Keycode k : {SDLK_s, SDLK_q, SDLK_x, SDLK_UP, SDLK_DOWN}) app.room_key_down(k, 0, false);
        d.hall.step(600);
        ASSERT_TRUE(app.room_chat().is_open() && app.room_chat().text() == "hello bob");
        ASSERT_TRUE(!app.map_select().is_locked() && app.state() == AppState::MapSelect && app.is_running() && app.network_active());
        ASSERT_TRUE(d.server.status("CHATUI-1").state == server::RoomState::Waiting);
        // Enter sends it (it is not START), closes the input, and the line is shown as said
        app.room_key_down(SDLK_RETURN, 0, false);
        ASSERT_FALSE(app.room_chat().is_open());
        ASSERT_TRUE(d.hall.until([&]() { return !d.bob.net.pregame_chat().empty(); }, 8000));
        ASSERT_EQ(lines(d.bob.net.pregame_chat()), (std::vector<std::string>{"0|Leader|hello bob"}));
        d.hall.step(30);
        ASSERT_EQ(app.map_select().room().status, std::string("You: hello bob"));
        ASSERT_TRUE(!app.map_select().is_locked() && d.server.status("CHATUI-1").state == server::RoomState::Waiting);
        // a click on the status line opens it; Esc closes it without sending anything
        ASSERT_FALSE(app.room_mouse_down(MapSelectScreen::BTN_START_X + 5, MapSelectScreen::BTN_START_Y + 5, SDL_BUTTON_LEFT));      // START's own click is the screen's
        ASSERT_FALSE(app.room_mouse_down(MapSelectScreen::LABEL_X + 5, MapSelectScreen::STATUS_Y - 2, SDL_BUTTON_LEFT));            // just above the label
        ASSERT_FALSE(app.room_mouse_down(MapSelectScreen::LABEL_X + MapSelectScreen::STATUS_W, MapSelectScreen::STATUS_Y + 5, SDL_BUTTON_LEFT));   // just right of it
        ASSERT_FALSE(app.room_mouse_down(MapSelectScreen::LABEL_X + 5, MapSelectScreen::STATUS_Y + MapSelectScreen::STATUS_H, SDL_BUTTON_LEFT));  // just under it
        ASSERT_FALSE(app.room_mouse_down(MapSelectScreen::LABEL_X + 5, MapSelectScreen::STATUS_Y + 5, SDL_BUTTON_RIGHT));           // the right button is not a click
        ASSERT_FALSE(app.room_chat().is_open());
        ASSERT_TRUE(app.room_mouse_down(MapSelectScreen::LABEL_X + 5, MapSelectScreen::STATUS_Y + 5, SDL_BUTTON_LEFT));
        ASSERT_TRUE(app.room_chat().is_open());
        app.room_text_input("never sent");
        app.room_key_down(SDLK_BACKSPACE, 0, false);
        ASSERT_EQ(app.room_chat().text(), std::string("never sen"));
        app.room_key_down(SDLK_BACKSPACE, KMOD_CTRL, false);                                  // Ctrl / Cmd with Backspace clears the line
        ASSERT_EQ(app.room_chat().text(), std::string());
        app.room_text_input("x");
        app.room_key_down(SDLK_ESCAPE, 0, false);
        ASSERT_FALSE(app.room_chat().is_open());
        d.hall.step(300);
        ASSERT_EQ(d.bob.net.pregame_chat().size(), size_t{1});                                // nothing was sent
        // an empty line sends nothing and is no START
        app.room_key_down(SDLK_t, 0, false);
        app.room_text_input("t");
        app.room_key_down(SDLK_RETURN, 0, false);
        ASSERT_FALSE(app.room_chat().is_open());
        d.hall.step(400);
        ASSERT_TRUE(d.bob.net.pregame_chat().size() == 1 && !app.map_select().is_locked() && d.server.status("CHATUI-1").state == server::RoomState::Waiting);
        // printable ASCII only, at most 100 characters; the status line shows the end of a long line
        app.room_key_down(SDLK_t, 0, false);
        app.room_text_input("t");
        app.room_text_input(std::string("a\x01\x7f") + "b" + "\xC3\xA9" + "c");
        ASSERT_EQ(app.room_chat().text(), std::string("abc"));
        app.room_text_input("start-marker");
        app.room_text_input(std::string(150, 'z'));
        ASSERT_EQ(app.room_chat().text().size(), size_t{100});
        ASSERT_TRUE(app.room_chat().text().rfind("abcstart-marker", 0) == 0);                // the line keeps its start (up to 100 characters)
        d.hall.step(30);
        ASSERT_TRUE(app.map_select().room().status_input && app.map_select().room().status == app.room_chat().text());     // the status line has the whole line; the screen fits it to its box (N5.53)
        app.room_key_down(SDLK_ESCAPE, 0, false);
        // the screen has its keys back (START's after the guard of 400 ms, N5.53): S starts the match for both
        d.hall.step(450);
        app.room_key_down(SDLK_s, 0, false);
        ASSERT_TRUE(d.hall.until([&]() { return app.state() == AppState::Playing && d.bob.net.phase() == net::NetGame::Phase::Playing; }, 15000));
        ASSERT_EQ(app.sim().roster_mask(), 0x03);
        app.quit();
    } TEST_END();

    TEST_CASE("N5.50 The Chat Input Through The Window's Own Event Loop: The Key, Text And Mouse Events That SDL Queues Reach It (T, The Typed Letters, Enter, A Click On The Status Line); A Local Game's Setup Screen And A Room That Is Gone Have No Chat, And An Input That Is Open When The Match Begins Closes") {
        {   // a local game: T is nobody's key
            Application local;
            ASSERT_TRUE(local.init(headless_config()));
            ASSERT_FALSE(local.room_chat_available());
            local.room_key_down(SDLK_t, 0, false);
            ASSERT_FALSE(local.room_chat().is_open());
            ASSERT_FALSE(local.room_mouse_down(MapSelectScreen::LABEL_X + 5, MapSelectScreen::STATUS_Y + 5, SDL_BUTTON_LEFT));
            local.quit();
        }
        Duo2 d;
        ASSERT_TRUE(d.server.make_room("CHATUI-2", 4));
        ASSERT_TRUE(d.app.init(join_config(d.server, "CHATUI-2", "Leader")));
        ASSERT_TRUE(d.hall.until([&]() { return d.app.net()->phase() == net::NetGame::Phase::Room && d.app.net()->is_leader(); }, 8000));
        ASSERT_TRUE(d.bob.net.join("127.0.0.1", d.server.port(), "Bob", 255, "CHATUI-2"));
        ASSERT_TRUE(d.hall.until([&]() { return d.bob.net.phase() == net::NetGame::Phase::Room && d.app.net()->room().slots[1].rtt_ms != net::kRttUnknown; }, 8000));
        d.hall.step(700);
        Application& app = d.app;
        const auto push = [](SDL_Event e) { ASSERT_TRUE(SDL_PushEvent(&e) == 1); };
        // the keyboard, as SDL delivers it: the key press and then the text of the same key, in one batch of events
        SDL_Event t_key;
        std::memset(&t_key, 0, sizeof(t_key));
        t_key.type = SDL_KEYDOWN;
        t_key.key.keysym.sym = SDLK_t;
        push(t_key);
        SDL_Event t_text;
        std::memset(&t_text, 0, sizeof(t_text));
        t_text.type = SDL_TEXTINPUT;
        std::snprintf(t_text.text.text, sizeof(t_text.text.text), "t");
        push(t_text);
        SDL_Event hi;
        std::memset(&hi, 0, sizeof(hi));
        hi.type = SDL_TEXTINPUT;
        std::snprintf(hi.text.text, sizeof(hi.text.text), "hi");
        push(hi);
        app.run_frame_with_delta(0.016f);
        ASSERT_TRUE(app.room_chat().is_open());
        ASSERT_EQ(app.room_chat().text(), std::string("hi"));                                 // the T of the key is not in it (the frame took all three events)
        SDL_Event s_key = t_key;
        s_key.key.keysym.sym = SDLK_s;
        push(s_key);                                                                          // an S while the line is typed: not a START
        app.run_frame_with_delta(0.016f);
        d.hall.step(500);
        ASSERT_TRUE(app.room_chat().is_open() && !app.map_select().is_locked() && d.server.status("CHATUI-2").state == server::RoomState::Waiting);
        SDL_Event enter = t_key;
        enter.key.keysym.sym = SDLK_RETURN;
        push(enter);
        app.run_frame_with_delta(0.016f);
        ASSERT_FALSE(app.room_chat().is_open());
        ASSERT_TRUE(d.hall.until([&]() { return !d.bob.net.pregame_chat().empty(); }, 8000));
        ASSERT_EQ(lines(d.bob.net.pregame_chat()), (std::vector<std::string>{"0|Leader|hi"}));
        // the mouse: a click on the status line (motion, press and release, as the window queues them)
        SDL_Event motion;
        std::memset(&motion, 0, sizeof(motion));
        motion.type = SDL_MOUSEMOTION;
        motion.motion.x = MapSelectScreen::LABEL_X + 20;
        motion.motion.y = MapSelectScreen::STATUS_Y + 10;
        push(motion);
        SDL_Event press = motion;
        press.type = SDL_MOUSEBUTTONDOWN;
        press.button.x = motion.motion.x;
        press.button.y = motion.motion.y;
        press.button.button = SDL_BUTTON_LEFT;
        press.button.clicks = 1;
        push(press);
        SDL_Event release = press;
        release.type = SDL_MOUSEBUTTONUP;
        push(release);
        app.run_frame_with_delta(0.016f);
        ASSERT_TRUE(app.room_chat().is_open());
        app.room_text_input("typing when the match begins");
        // the match begins while the line is open (the leader's START by the button's click: the mouse works while the input is open): the input closes
        const int32_t sx = MapSelectScreen::BTN_START_X + 5;
        const int32_t sy = MapSelectScreen::BTN_START_Y + 5;
        app.map_select().handle_mouse_motion(sx, sy);
        app.map_select().handle_mouse_down(sx, sy, 1);
        app.map_select().handle_mouse_up(sx, sy, 1);
        ASSERT_TRUE(d.hall.until([&]() { return app.state() == AppState::Playing && d.bob.net.phase() == net::NetGame::Phase::Playing; }, 15000));
        ASSERT_FALSE(app.room_chat().is_open());
        ASSERT_FALSE(app.room_chat_available());                                              // (a match has its own chat: the HUD's)
        app.room_key_down(SDLK_t, 0, false);
        ASSERT_FALSE(app.room_chat().is_open());
        app.quit();
    } TEST_END();

    TEST_CASE("N5.51 The Match's Chat Log Starts With The Waiting Room's Lines: What The Players Said Before START, Their Own Included, Is The First Of The Log At The Match (In Order, Under The Names Of The Players; The Room's Own Notices Are Not Repeated); A Line Of The Match Follows") {
        Server server;
        server::RoomSpec spec;
        spec.code = "CHATUI-3";
        spec.map = "TINY.LVL";
        spec.players = 4;
        ASSERT_TRUE(server.mgr.create_room(spec, server.now).ok);
        Application app;
        ASSERT_TRUE(app.init(join_config(server, "CHATUI-3", "Leader")));
        Peer bob;
        Hall hall{server, &app, {&bob}};
        ASSERT_TRUE(hall.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && app.net()->is_leader(); }, 8000));
        ASSERT_TRUE(bob.net.join("127.0.0.1", server.port(), "Bob", 255, "CHATUI-3"));
        ASSERT_TRUE(hall.until([&]() { return bob.net.phase() == net::NetGame::Phase::Room && app.net()->room().slots[1].rtt_ms != net::kRttUnknown; }, 8000));
        hall.step(700);
        app.room_key_down(SDLK_t, 0, false);
        app.room_text_input("t");
        app.room_text_input("ready when you are");
        app.room_key_down(SDLK_RETURN, 0, false);
        ASSERT_TRUE(hall.until([&]() { return bob.net.pregame_chat().size() == 1; }, 8000));
        ASSERT_TRUE(bob.net.chat("one moment"));
        ASSERT_TRUE(hall.until([&]() { return app.net()->pregame_chat().size() == 2; }, 8000));
        ASSERT_EQ(lines(app.net()->pregame_chat()), (std::vector<std::string>{"0|Leader|ready when you are", "1|Bob|one moment"}));
        hall.step(450);                                                                       // (START's guard after the input closed: N5.53)
        app.room_key_down(SDLK_s, 0, false);
        ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 15000));
        const std::string log = app.hud().chat_transcript("t");
        const size_t first = log.find("Leader: ready when you are\n");
        const size_t second = log.find("Bob: one moment\n");
        ASSERT_TRUE(first != std::string::npos && second != std::string::npos && first < second);
        ASSERT_TRUE(log.find("(room)") == std::string::npos);
        // and the chat of the match goes on in the same log
        ASSERT_TRUE(bob.net.chat("in the match"));
        ASSERT_TRUE(hall.until([&]() { return app.hud().chat_transcript("t").find("Bob: in the match\n") != std::string::npos; }, 8000));
        app.quit();
        // the room's notices are not part of the log: a fill in a room with Fog of War is refused with a notice, the match then starts with the people who are there
        Server fog_server;
        server::RoomSpec fog_spec;
        fog_spec.code = "CHATUI-4";
        fog_spec.map = "TINY.LVL";
        fog_spec.players = 4;
        fog_spec.fog = true;
        ASSERT_TRUE(fog_server.mgr.create_room(fog_spec, fog_server.now).ok);
        ApplicationConfig cfg = join_config(fog_server, "CHATUI-4", "Leader");
        cfg.fill_bots = net::FillLevel::Medium;
        Application fog_app;
        ASSERT_TRUE(fog_app.init(cfg));
        Peer cat;
        Hall fog_hall{fog_server, &fog_app, {&cat}};
        ASSERT_TRUE(fog_hall.until([&]() { return fog_app.net()->phase() == net::NetGame::Phase::Room && fog_app.net()->is_leader(); }, 8000));
        ASSERT_TRUE(cat.net.join("127.0.0.1", fog_server.port(), "Cat", 255, "CHATUI-4"));
        ASSERT_TRUE(fog_hall.until([&]() { return cat.net.phase() == net::NetGame::Phase::Room && fog_app.net()->room().slots[1].rtt_ms != net::kRttUnknown; }, 8000));
        fog_hall.step(700);
        fog_app.room_key_down(SDLK_s, 0, false);
        ASSERT_TRUE(fog_hall.until([&]() { return fog_app.state() == AppState::Playing && cat.net.phase() == net::NetGame::Phase::Playing; }, 15000));
        ASSERT_TRUE(!fog_app.net()->pregame_chat().empty() && fog_app.net()->pregame_chat()[0].notice());        // the room did say it
        ASSERT_EQ(fog_app.hud().chat_transcript("t").find(net::kNoticeFillFog), std::string::npos);              // the match's log does not repeat it
        ASSERT_EQ(fog_app.sim().roster_mask(), 0x03);
        fog_app.quit();
    } TEST_END();

    TEST_CASE("N5.52 The Status Line Of A Setup Screen That Can START With Bots Says What START Will Do: \"Press START: the empty seats get Medium bots.\" For The Leader Of A Server's Room And The Host Of A Room On The Local Network With A Fill Level, The Original's Prompt Without One, \"Fog of War is on, so START seats no bots.\" In A Room With Fog; A Guest Keeps Its Own Line") {
        // the text as a function
        ASSERT_EQ(net::NetGame::start_prompt(net::FillLevel::Medium, false), std::string("Press START: the empty seats get Medium bots."));
        ASSERT_EQ(net::NetGame::start_prompt(net::FillLevel::Easy, false), std::string("Press START: the empty seats get Easy bots."));
        ASSERT_EQ(net::NetGame::start_prompt(net::FillLevel::Hard, false), std::string("Press START: the empty seats get Hard bots."));
        ASSERT_EQ(net::NetGame::start_prompt(net::FillLevel::Hard, true), std::string("Fog of War is on, so START seats no bots."));
        ASSERT_EQ(net::NetGame::start_prompt(net::FillLevel::None, false), std::string("Press START when all players' thumbs have appeared."));
        {   // the leader of a server's room, and a guest of it
            Server server;
            ASSERT_TRUE(server.make_room("PROMPT-1", 4));
            ApplicationConfig cfg = join_config(server, "PROMPT-1", "Leader");
            cfg.fill_bots = net::FillLevel::Medium;
            Application app;
            ASSERT_TRUE(app.init(cfg));
            Peer bob;
            Hall hall{server, &app, {&bob}};
            ASSERT_TRUE(hall.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && app.net()->is_leader(); }, 8000));
            hall.step(100);
            ASSERT_EQ(app.map_select().room().status, std::string("Press START: the empty seats get Medium bots."));
            app.set_fill_bots(net::FillLevel::Hard);                                          // the screens' choice changes the line at the next frame
            hall.step(30);
            ASSERT_EQ(app.map_select().room().status, std::string("Press START: the empty seats get Hard bots."));
            app.set_fill_bots(net::FillLevel::None);
            hall.step(30);
            ASSERT_EQ(app.map_select().room().status, std::string("Press START when all players' thumbs have appeared."));
            app.set_fill_bots(net::FillLevel::Easy);
            ASSERT_TRUE(bob.net.join("127.0.0.1", server.port(), "Bob", 255, "PROMPT-1"));
            ASSERT_TRUE(hall.until([&]() { return bob.net.phase() == net::NetGame::Phase::Room; }, 8000));
            hall.step(100);
            ASSERT_EQ(bob.net.status_text(), std::string("Waiting for the host to start the game..."));       // (a guest with a fill level of its own: no START, no line about it)
            bob.net.set_fill_bots(net::FillLevel::Hard);
            hall.step(100);
            ASSERT_FALSE(bob.net.status_text().find("START") != std::string::npos && bob.net.status_text().find("bots") != std::string::npos);
            app.quit();
        }
        {   // a room with Fog of War
            Server server;
            server::RoomSpec spec;
            spec.code = "PROMPT-2";
            spec.map = "TINY.LVL";
            spec.players = 4;
            spec.fog = true;
            ASSERT_TRUE(server.mgr.create_room(spec, server.now).ok);
            ApplicationConfig cfg = join_config(server, "PROMPT-2", "Leader");
            cfg.fill_bots = net::FillLevel::Hard;
            Application app;
            ASSERT_TRUE(app.init(cfg));
            Hall hall{server, &app, {}};
            ASSERT_TRUE(hall.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && app.net()->is_leader(); }, 8000));
            hall.step(100);
            ASSERT_EQ(app.map_select().room().status, std::string("Fog of War is on, so START seats no bots."));
            app.quit();
        }
        {   // the host of a room on the local network
            ApplicationConfig cfg = headless_config();
            cfg.net_role = ApplicationConfig::NetRole::Host;
            cfg.net_port = 0;
            cfg.net_loopback_only = true;
            cfg.player_name = "Alice";
            cfg.fill_bots = net::FillLevel::Medium;
            Application app;
            ASSERT_TRUE(app.init(cfg));
            for (int i = 0; i < 20; ++i) app.pump_network(0.010f);
            ASSERT_EQ(app.map_select().room().status, std::string("Press START: the empty seats get Medium bots."));
            app.set_fill_bots(net::FillLevel::None);
            for (int i = 0; i < 20; ++i) app.pump_network(0.010f);
            ASSERT_EQ(app.map_select().room().status, std::string("Press START when all players' thumbs have appeared."));
            app.quit();
        }
    } TEST_END();

    // SDL events for the tests that go through the window's own event loop
    const auto push_key = [](SDL_Keycode sym, bool repeat) {
        SDL_Event e;
        std::memset(&e, 0, sizeof(e));
        e.type = SDL_KEYDOWN;
        e.key.keysym.sym = sym;
        e.key.repeat = repeat ? 1 : 0;
        return SDL_PushEvent(&e) == 1;
    };
    const auto push_button = [](Uint32 type, int32_t x, int32_t y) {
        SDL_Event e;
        std::memset(&e, 0, sizeof(e));
        e.type = type;
        e.button.x = x;
        e.button.y = y;
        e.button.button = SDL_BUTTON_LEFT;
        e.button.clicks = 1;
        return SDL_PushEvent(&e) == 1;
    };
    // a room of four with the application as its leader and Bob in it, the setup screen shown
    const auto open_room = [&](Duo2& d, const std::string& code) {
        if (!d.server.make_room(code, 4)) return false;
        if (!d.app.init(join_config(d.server, code, "Leader"))) return false;
        if (!d.hall.until([&]() { return d.app.net()->phase() == net::NetGame::Phase::Room && d.app.net()->is_leader(); }, 8000)) return false;
        if (!d.bob.net.join("127.0.0.1", d.server.port(), "Bob", 255, code)) return false;
        if (!d.hall.until([&]() { return d.bob.net.phase() == net::NetGame::Phase::Room && d.app.net()->room().slots[1].rtt_ms != net::kRttUnknown; }, 8000)) return false;
        d.hall.step(700);
        return true;
    };

    TEST_CASE("N5.53 After The Chat Input Closes, START Is Not For The Next Key (A Review Fix): T, A Line, Enter, Enter (A Second Enter Right After) Starts Nothing; T, Enter, Enter Starts Nothing; Esc And Then S Starts Nothing; Neither Does The Keypad's Enter Or A Repeat; 350 ms After The Closing Still Nothing, 450 ms After It A Fresh Key Starts The Match; A Held Enter Starts Nothing On The Leader's Or A LAN Host's Screen (A Screen Of A Room) But Still Starts A Local Game (The Original's Screen)") {
        Duo2 d;
        ASSERT_TRUE(open_room(d, "GUARD-1"));
        Application& app = d.app;
        const auto room_state = [&]() { return d.server.status("GUARD-1").state; };
        const auto still_waiting = [&]() { return room_state() == server::RoomState::Waiting && !app.map_select().is_locked() && app.state() == AppState::MapSelect; };
        // RP1: T, a line, Enter (it sends and closes), a second Enter 20 ms later
        app.room_key_down(SDLK_t, 0, false);
        app.room_text_input("t");
        app.room_text_input("hi bob");
        app.room_key_down(SDLK_RETURN, 0, false);
        ASSERT_FALSE(app.room_chat().is_open());
        d.hall.step(20);
        app.room_key_down(SDLK_RETURN, 0, false);
        d.hall.step(1500);
        ASSERT_TRUE(still_waiting());
        ASSERT_EQ(d.bob.net.pregame_chat().size(), size_t{1});                                 // (the line did go: the second Enter was no line and no START)
        // RP2: T, nothing typed, Enter (it closes, sends nothing), Enter
        app.room_key_down(SDLK_t, 0, false);
        app.room_text_input("t");
        app.room_key_down(SDLK_RETURN, 0, false);
        app.room_key_down(SDLK_RETURN, 0, false);
        d.hall.step(1500);
        ASSERT_TRUE(still_waiting());
        // RP4: Esc, then S (the habit of typing "ss"), and the other START keys, a repeat among them
        for (const SDL_Keycode key : {SDLK_s, SDLK_RETURN, SDLK_KP_ENTER}) {
            app.room_key_down(SDLK_t, 0, false);
            app.room_text_input("t");
            app.room_text_input("oops");
            app.room_key_down(SDLK_ESCAPE, 0, false);
            app.room_key_down(key, 0, false);
            app.room_key_down(key, 0, true);
            d.hall.step(450);                                                                  // (the guard is over by the next round)
            ASSERT_TRUE(still_waiting());
        }
        ASSERT_EQ(d.bob.net.pregame_chat().size(), size_t{1});                                 // (nothing of "oops" was sent)
        // the other keys are not held back: Q leaves, but the map keys and the like are the screen's at once (here the Down key moves nothing for a leader, but it is not swallowed: T works at once)
        app.room_key_down(SDLK_t, 0, false);
        app.room_text_input("t");
        app.room_key_down(SDLK_ESCAPE, 0, false);
        app.room_key_down(SDLK_t, 0, false);                                                    // T right after the closing opens it again
        ASSERT_TRUE(app.room_chat().is_open());
        app.room_key_down(SDLK_ESCAPE, 0, false);
        // a held Enter that was never a press starts nothing, now or later (the leader's screen)
        d.hall.step(450);
        for (int i = 0; i < 5; ++i) app.room_key_down(SDLK_RETURN, 0, true);
        app.room_key_down(SDLK_s, 0, true);
        d.hall.step(1000);
        ASSERT_TRUE(still_waiting());
        for (const SDL_Keycode key : {SDLK_RETURN, SDLK_KP_ENTER, SDLK_s}) ASSERT_TRUE(push_key(key, true));      // the same through the window's own event loop: SDL says repeat
        app.run_frame_with_delta(0.016f);
        d.hall.step(1000);
        ASSERT_TRUE(still_waiting());
        // the boundary: after Esc, 350 ms later the key is still held back, 450 ms after it is a fresh decision
        app.room_key_down(SDLK_t, 0, false);
        app.room_text_input("t");
        app.room_key_down(SDLK_ESCAPE, 0, false);
        d.hall.step(350);
        app.room_key_down(SDLK_RETURN, 0, false);
        d.hall.step(200);
        ASSERT_TRUE(still_waiting());                                                           // (the Enter at 350 ms was swallowed; the clock has moved on to 550 ms)
        // the guard is counted from the CLOSING, not from the last key: another Enter 100 ms after the swallowed one is still after more than 400 ms since the closing: it is a press
        app.room_key_down(SDLK_RETURN, 0, false);
        ASSERT_TRUE(d.hall.until([&]() { return app.state() == AppState::Playing && d.bob.net.phase() == net::NetGame::Phase::Playing; }, 15000));
        ASSERT_EQ(app.sim().roster_mask(), 0x03);
        app.quit();

        {   // a LAN host: the room is the host's own, with a guest who is ready; a held Enter starts nothing, a press after the closing guard does
            ApplicationConfig cfg = headless_config();
            cfg.net_role = ApplicationConfig::NetRole::Host;
            cfg.net_port = 0;
            cfg.net_loopback_only = true;
            cfg.player_name = "Alice";
            Application host;
            ASSERT_TRUE(host.init(cfg));
            Peer guest;
            ASSERT_TRUE(guest.net.join("127.0.0.1", host.net()->listen_port(), "Bob"));
            Duo duo{host, guest};
            ASSERT_TRUE(duo.until([&]() { return host.net()->room().slots[1].state == net::SlotState::Client && host.net()->can_start() && host.room_chat_available(); }, 8000));
            duo.step(700);
            for (int i = 0; i < 5; ++i) host.room_key_down(SDLK_RETURN, 0, true);
            host.room_key_down(SDLK_s, 0, true);
            host.room_key_down(SDLK_KP_ENTER, 0, true);
            duo.step(1000);
            ASSERT_TRUE(host.state() == AppState::MapSelect && !host.map_select().is_locked() && guest.net.phase() == net::NetGame::Phase::Room);     // nothing started
            host.room_key_down(SDLK_t, 0, false);                                                // the same guard on a LAN host: a line, Enter, Enter
            host.room_text_input("t");
            host.room_text_input("ready?");
            host.room_key_down(SDLK_RETURN, 0, false);
            host.room_key_down(SDLK_RETURN, 0, false);
            duo.step(600);
            ASSERT_TRUE(host.state() == AppState::MapSelect && !host.map_select().is_locked() && guest.net.phase() == net::NetGame::Phase::Room);
            host.room_key_down(SDLK_RETURN, 0, false);                                           // a fresh press, long after the closing: START
            ASSERT_TRUE(duo.until([&]() { return host.state() == AppState::Playing && guest.net.phase() == net::NetGame::Phase::Playing; }, 8000));
            host.quit();
        }
        {   // a local game's screen is the original's: a held Enter (the repeat) STARTS it, as it always did (the rule of the repeat is for the screens of a room only; there is no chat here)
            Application local;
            ASSERT_TRUE(local.init(headless_config()));
            ASSERT_EQ(local.state(), AppState::MapSelect);
            ASSERT_FALSE(local.room_chat_available());
            local.room_key_down(SDLK_RETURN, 0, true);
            ASSERT_EQ(local.state(), AppState::Playing);
            local.quit();
        }
        {   // ... and so does the repeat of the keypad's Enter and of S; and a press starts it too
            for (const SDL_Keycode key : {SDLK_KP_ENTER, SDLK_s}) {
                Application local;
                ASSERT_TRUE(local.init(headless_config()));
                local.room_key_down(key, 0, true);
                ASSERT_EQ(local.state(), AppState::Playing);
                local.quit();
            }
            Application local;
            ASSERT_TRUE(local.init(headless_config()));
            local.room_key_down(SDLK_RETURN, 0, false);
            ASSERT_EQ(local.state(), AppState::Playing);
            local.quit();
        }
    } TEST_END();

    TEST_CASE("N5.54 A Player With No Keyboard Can Close The Chat Input (A Review Fix): A Second Click On The Status Line Closes It And Sends Nothing, A Click Anywhere Else Closes It Too (It Is Not The Screen's Click: A Click On START Does Not Start The Match Through An Open Input), The Release Of A Press That Began Before It Opened Fires Nothing; The Same Through The Window's Own Event Loop") {
        Duo2 d;
        ASSERT_TRUE(open_room(d, "GUARD-2"));
        Application& app = d.app;
        const int32_t line_x = MapSelectScreen::LABEL_X + 20;
        const int32_t line_y = MapSelectScreen::STATUS_Y + 10;
        const int32_t sx = MapSelectScreen::BTN_START_X + 5;
        const int32_t sy = MapSelectScreen::BTN_START_Y + 5;
        const auto quiet = [&]() { return d.server.status("GUARD-2").state == server::RoomState::Waiting && !app.map_select().is_locked() && app.state() == AppState::MapSelect; };
        // a click opens it; its release is the input's (nothing for the screen)
        ASSERT_TRUE(app.room_mouse_down(line_x, line_y, SDL_BUTTON_LEFT));
        ASSERT_TRUE(app.room_chat().is_open());
        ASSERT_TRUE(app.room_mouse_up(line_x, line_y, SDL_BUTTON_LEFT));
        app.room_text_input("typed with a finger");
        ASSERT_FALSE(app.room_mouse_down(line_x, line_y, SDL_BUTTON_RIGHT));                  // the right button is no click
        ASSERT_TRUE(app.room_chat().is_open());
        // a second click on the status line closes it and sends nothing
        ASSERT_TRUE(app.room_mouse_down(line_x, line_y, SDL_BUTTON_LEFT));
        ASSERT_FALSE(app.room_chat().is_open());
        ASSERT_TRUE(app.room_mouse_up(line_x, line_y, SDL_BUTTON_LEFT));
        ASSERT_FALSE(app.room_mouse_up(line_x, line_y, SDL_BUTTON_LEFT));                     // (closed, and nothing pending: a release now is the screen's)
        d.hall.step(500);
        ASSERT_TRUE(d.bob.net.pregame_chat().empty() && quiet());
        ASSERT_EQ(app.room_chat().text(), std::string());
        // the closing click starts the guard of START's keys as Enter and Esc do: Enter right after a click that closed the input is no START
        ASSERT_TRUE(app.room_mouse_down(line_x, line_y, SDL_BUTTON_LEFT));
        ASSERT_TRUE(app.room_mouse_up(line_x, line_y, SDL_BUTTON_LEFT));
        ASSERT_TRUE(app.room_mouse_down(line_x, line_y, SDL_BUTTON_LEFT));                  // (the second click closes it)
        ASSERT_TRUE(app.room_mouse_up(line_x, line_y, SDL_BUTTON_LEFT));
        ASSERT_FALSE(app.room_chat().is_open());
        app.room_key_down(SDLK_RETURN, 0, false);
        app.room_key_down(SDLK_s, 0, false);
        d.hall.step(300);
        ASSERT_TRUE(quiet());
        d.hall.step(300);                                                                       // (more than 400 ms in all since the click: START keys are the screen's again, but not pressed here)
        // a click elsewhere closes it too (the map's box, the empty middle of the screen)
        for (const auto& where : {std::pair<int32_t, int32_t>{100, 320}, std::pair<int32_t, int32_t>{300, 150}, std::pair<int32_t, int32_t>{620, 470}}) {
            ASSERT_TRUE(app.room_mouse_down(line_x, line_y, SDL_BUTTON_LEFT));
            ASSERT_TRUE(app.room_mouse_up(line_x, line_y, SDL_BUTTON_LEFT));
            app.room_text_input("words");
            ASSERT_TRUE(app.room_mouse_down(where.first, where.second, SDL_BUTTON_LEFT));
            ASSERT_FALSE(app.room_chat().is_open());
            ASSERT_TRUE(app.room_mouse_up(where.first, where.second, SDL_BUTTON_LEFT));
            ASSERT_TRUE(quiet());
        }
        // a click on START while the input is open: it closes the input, the match does not start; the release fires nothing either
        ASSERT_TRUE(app.room_mouse_down(line_x, line_y, SDL_BUTTON_LEFT));
        ASSERT_TRUE(app.room_mouse_up(line_x, line_y, SDL_BUTTON_LEFT));
        app.room_text_input("a line");
        ASSERT_TRUE(app.room_mouse_down(sx, sy, SDL_BUTTON_LEFT));
        ASSERT_FALSE(app.room_chat().is_open());
        ASSERT_FALSE(app.map_select().start_button().pressed());
        ASSERT_TRUE(app.room_mouse_up(sx, sy, SDL_BUTTON_LEFT));
        d.hall.step(1500);
        ASSERT_TRUE(quiet() && d.bob.net.pregame_chat().empty());
        // a press that began on START BEFORE the input opened: T opens the input, the release is not START's click
        app.map_select().handle_mouse_motion(sx, sy);
        app.map_select().handle_mouse_down(sx, sy, SDL_BUTTON_LEFT);
        ASSERT_TRUE(app.map_select().start_button().pressed());
        app.room_key_down(SDLK_t, 0, false);
        ASSERT_TRUE(app.room_chat().is_open());
        ASSERT_TRUE(app.room_mouse_up(sx, sy, SDL_BUTTON_LEFT));
        ASSERT_FALSE(app.map_select().start_button().pressed());
        d.hall.step(1500);
        ASSERT_TRUE(quiet() && app.room_chat().is_open());                                      // (the input is still open: a release is no click on it)
        app.room_key_down(SDLK_ESCAPE, 0, false);
        // the same through the window's own event loop (motion, press, release as SDL queues them): a click on START with the input open starts nothing
        SDL_Delay(600);                                                                         // (the double-click time of the click that closed the input before is over: its rest is not this click, N5.73)
        ASSERT_TRUE(push_button(SDL_MOUSEBUTTONDOWN, line_x, line_y));
        ASSERT_TRUE(push_button(SDL_MOUSEBUTTONUP, line_x, line_y));
        app.run_frame_with_delta(0.016f);
        ASSERT_TRUE(app.room_chat().is_open());
        ASSERT_TRUE(push_button(SDL_MOUSEBUTTONDOWN, sx, sy));
        ASSERT_TRUE(push_button(SDL_MOUSEBUTTONUP, sx, sy));
        app.run_frame_with_delta(0.016f);
        ASSERT_FALSE(app.room_chat().is_open());
        d.hall.step(1500);
        ASSERT_TRUE(quiet());
        // and with the input closed the same click is START: the match begins (the mouse has no guard: a click is a decision; only the rest of the click that closed the input is not one, N5.73:
        // a click after its double-click time is)
        SDL_Delay(600);
        ASSERT_TRUE(push_button(SDL_MOUSEBUTTONDOWN, sx, sy));
        ASSERT_TRUE(push_button(SDL_MOUSEBUTTONUP, sx, sy));
        app.run_frame_with_delta(0.016f);
        ASSERT_TRUE(d.hall.until([&]() { return app.state() == AppState::Playing && d.bob.net.phase() == net::NetGame::Phase::Playing; }, 15000));
        app.quit();
    } TEST_END();

    TEST_CASE("N5.55 The Match's Chat Log Gives A Seeded Line To The One Who Said It (A Review Fix): Bob Speaks In The Waiting Room And Leaves, Cat Takes His Seat: The Log Says Bob, Not Cat; A Line Whose Name Is Empty Falls Back To The Seat's Name") {
        Server server;
        server::RoomSpec spec;
        spec.code = "SEED-1";
        spec.map = "TINY.LVL";
        spec.players = 4;
        ASSERT_TRUE(server.mgr.create_room(spec, server.now).ok);
        Application app;
        ASSERT_TRUE(app.init(join_config(server, "SEED-1", "Leader")));
        Peer bob, cat;
        Hall hall{server, &app, {&bob, &cat}};
        ASSERT_TRUE(hall.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && app.net()->is_leader(); }, 8000));
        ASSERT_TRUE(bob.net.join("127.0.0.1", server.port(), "Bob", 255, "SEED-1"));
        ASSERT_TRUE(hall.until([&]() { return bob.net.phase() == net::NetGame::Phase::Room && app.net()->room().slots[1].rtt_ms != net::kRttUnknown; }, 8000));
        ASSERT_TRUE(bob.net.chat("I am Bob and I have something to say"));
        ASSERT_TRUE(hall.until([&]() { return app.net()->pregame_chat().size() == 1; }, 8000));
        bob.net.leave();                                                                     // Bob goes, his seat is free again
        hall.step(500);
        ASSERT_TRUE(cat.net.join("127.0.0.1", server.port(), "Cat", 255, "SEED-1"));
        ASSERT_TRUE(hall.until([&]() { return cat.net.phase() == net::NetGame::Phase::Room && app.net()->room().slots[1].rtt_ms != net::kRttUnknown; }, 8000));
        ASSERT_EQ(cat.net.my_seat(), uint8_t{1});                                            // Cat sits where Bob sat
        ASSERT_TRUE(cat.net.chat("Cat here"));
        ASSERT_TRUE(hall.until([&]() { return app.net()->pregame_chat().size() == 2; }, 8000));
        hall.step(700);
        app.room_key_down(SDLK_s, 0, false);
        ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing && cat.net.phase() == net::NetGame::Phase::Playing; }, 15000));
        ASSERT_EQ(app.sim().get_player_name(1), std::string("Cat"));                         // (the seat has another player now)
        const std::string log = app.hud().chat_transcript("t");
        ASSERT_TRUE(log.find("Bob: I am Bob and I have something to say\n") != std::string::npos);
        ASSERT_TRUE(log.find("Cat: I am Bob") == std::string::npos);
        ASSERT_TRUE(log.find("Cat: Cat here\n") != std::string::npos);
        ASSERT_TRUE(log.find("Bob: I am Bob") < log.find("Cat: Cat here"));
        app.quit();
    } TEST_END();

    TEST_CASE("N5.56 The Room's Chat Does Not Flood The Program's Log (A Review Fix): At Most 20 Lines At Once And 10 A Second, The Lines Beyond Are Counted And One Line Says How Many Were Not Logged; An Honest Room Is Logged In Full; The Application Prints Through It") {
        {   // the model
            RoomChatLog log;
            std::vector<net::ChatLine> flood;
            for (int i = 0; i < 300; ++i) flood.push_back(net::ChatLine{static_cast<uint8_t>(i % 4), "P" + std::to_string(i % 4), "line " + std::to_string(i)});
            std::vector<std::string> out = log.take(flood, 1000);
            ASSERT_EQ(out.size(), size_t{RoomChatLog::kBurst});                                // the burst, and nothing for the summary (the budget is empty)
            ASSERT_EQ(out.front(), std::string("P0: line 0"));
            ASSERT_EQ(out.back(), std::string("P3: line 19"));
            ASSERT_EQ(log.left_out(), uint64_t{300 - RoomChatLog::kBurst + 0});
            out = log.take({}, 1000);                                                          // no time has passed: still nothing
            ASSERT_TRUE(out.empty());
            out = log.take({}, 1300);                                                          // 300 ms: 3 tokens: the summary, once
            ASSERT_EQ(out.size(), size_t{1});
            ASSERT_EQ(out[0], std::string("(280 more lines were not logged)"));
            ASSERT_EQ(log.left_out(), uint64_t{0});
            ASSERT_TRUE(log.take({}, 1400).empty());                                           // (reported: nothing more to say)
            // an honest room: 40 lines over 20 seconds are all there, no summary
            RoomChatLog calm;
            size_t printed = 0;
            for (int i = 0; i < 40; ++i) {
                const std::vector<std::string> one = calm.take({net::ChatLine{0, "Ann", "hello " + std::to_string(i)}}, 5000 + static_cast<uint32_t>(i) * 500u);
                ASSERT_TRUE(one.size() == 1 && one[0] == "Ann: hello " + std::to_string(i));
                printed += one.size();
            }
            ASSERT_EQ(printed, size_t{40});
            ASSERT_EQ(calm.take({net::ChatLine{255, "", "Bots cannot play with Fog of War."}}, 30000)[0], std::string("(room): Bots cannot play with Fog of War."));
        }
        {   // the application, joined to a host that says 300 lines at once (its own lines have no budget): what reaches stderr is bounded
            Peer host;
            ASSERT_TRUE(host.net.host(0, "Alice", true));
            host.net.set_map("TINY.LVL");
            ApplicationConfig cfg = headless_config();
            cfg.net_role = ApplicationConfig::NetRole::Join;
            cfg.net_address = "127.0.0.1";
            cfg.net_port = host.net.listen_port();
            cfg.player_name = "Guest";
            Application app;
            ASSERT_TRUE(app.init(cfg));
            Duo duo{app, host};
            ASSERT_TRUE(duo.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && host.net.room().slots[1].rtt_ms != net::kRttUnknown; }, 8000));
            std::ostringstream captured;
            std::streambuf* const saved = std::cerr.rdbuf(captured.rdbuf());
            const double clock_before = app.net_clock_ms();
            for (int i = 0; i < 300; ++i) host.net.chat("flood " + std::to_string(i));
            duo.step(300);
            const double elapsed_s = (app.net_clock_ms() - clock_before) / 1000.0;
            std::cerr.rdbuf(saved);
            size_t printed = 0;
            bool summary = false;
            std::istringstream in(captured.str());
            for (std::string line; std::getline(in, line);) {
                if (line.find("Room chat: Alice: flood ") != std::string::npos) ++printed;
                if (line.find("more lines were not logged") != std::string::npos) summary = true;
            }
            ASSERT_TRUE(printed >= RoomChatLog::kBurst && printed <= RoomChatLog::kBurst + static_cast<size_t>(RoomChatLog::kPerSecond * elapsed_s) + 1);      // the burst and the rate: not 300
            ASSERT_TRUE(summary);
            app.quit();
        }
    } TEST_END();

    TEST_CASE("N5.57 Typing While The Map Loads: The Leader Presses START, The Room Is Loading (A Guest Has Not Reported Yet), T Still Opens The Input, The Line Reaches The Other Player, And The Input Closes When The Match Begins") {
        Duo2 d;
        ASSERT_TRUE(open_room(d, "LOAD-1"));
        Application& app = d.app;
        d.bob.hold_report = true;                                                             // Bob loads the map and does not report: the room stays in Loading
        app.room_key_down(SDLK_s, 0, false);
        ASSERT_TRUE(d.hall.until([&]() { return d.bob.report_pending; }, 8000));
        ASSERT_TRUE(d.hall.until([&]() { return app.net()->phase() == net::NetGame::Phase::Loading; }, 8000));
        ASSERT_TRUE(app.state() == AppState::MapSelect && app.room_chat_available());
        app.room_key_down(SDLK_t, 0, false);
        ASSERT_TRUE(app.room_chat().is_open());
        app.room_text_input("t");
        app.room_text_input("loading, loading");
        app.room_key_down(SDLK_RETURN, 0, false);
        ASSERT_FALSE(app.room_chat().is_open());
        ASSERT_TRUE(d.hall.until([&]() { return !d.bob.net.pregame_chat().empty(); }, 8000));
        ASSERT_EQ(lines(d.bob.net.pregame_chat()), (std::vector<std::string>{"0|Leader|loading, loading"}));
        ASSERT_EQ(app.map_select().room().status, std::string("You: loading, loading"));
        // a click opens it while loading too, and the match beginning closes it
        ASSERT_TRUE(app.room_mouse_down(MapSelectScreen::LABEL_X + 5, MapSelectScreen::STATUS_Y + 5, SDL_BUTTON_LEFT));
        ASSERT_TRUE(app.room_chat().is_open());
        app.room_text_input("still typing");
        d.bob.report_pending = false;
        d.bob.net.report_loaded(true);
        ASSERT_TRUE(d.hall.until([&]() { return app.state() == AppState::Playing && d.bob.net.phase() == net::NetGame::Phase::Playing; }, 15000));
        ASSERT_FALSE(app.room_chat().is_open());
        ASSERT_FALSE(app.room_chat_available());
        app.quit();
    } TEST_END();

    TEST_CASE("N5.58 The Host Of A Room On The Local Network With A Fill Level And Fog Of War: START Seats No Bots And The Status Line Says Why (\"Bots cannot play with Fog of War.\"); With A Guest In The Room The Match Starts Without Bots, Alone It Is The Can't-Go Cue") {
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Host;
        cfg.net_port = 0;
        cfg.net_loopback_only = true;
        cfg.player_name = "Alice";
        cfg.fill_bots = net::FillLevel::Medium;
        Application app;
        ASSERT_TRUE(app.init(cfg));
        for (int i = 0; i < 30; ++i) app.pump_network(0.010f);
        app.map_select().set_selected_index(0);
        app.net()->set_fog(true);                                                              // (what the Fog of War: On button does for a host)
        for (int i = 0; i < 30; ++i) app.pump_network(0.010f);
        ASSERT_EQ(app.map_select().room().status, std::string("Fog of War is on, so START seats no bots."));       // the prompt says it before anybody presses anything
        app.map_select().handle_key_down(SDLK_RETURN);                                        // alone: no bots with fog, one person: the can't-go cue
        for (int i = 0; i < 3; ++i) app.pump_network(0.010f);
        ASSERT_EQ(app.map_select().room().status, std::string(net::kNoticeFillFog));          // the notice replaced the prompt at once: START said no, and why
        for (uint8_t seat = 1; seat < 4; ++seat) ASSERT_TRUE(app.net()->room().slots[seat].state == net::SlotState::Empty);
        ASSERT_FALSE(app.map_select().is_locked());
        ASSERT_EQ(app.net()->phase(), net::NetGame::Phase::Room);
        Peer bob;
        ASSERT_TRUE(bob.net.join("127.0.0.1", app.net()->listen_port(), "Bob"));
        Duo duo{app, bob};
        ASSERT_TRUE(duo.until([&]() { return app.net()->room().slots[1].state == net::SlotState::Client && app.net()->room().slots[1].rtt_ms != net::kRttUnknown; }, 8000));
        app.map_select().handle_key_down(SDLK_RETURN);
        ASSERT_TRUE(duo.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 8000));
        ASSERT_EQ(app.sim().roster_mask(), 0x03);                                              // two people, no bots
        ASSERT_TRUE(app.bots() == nullptr);
        app.quit();
    } TEST_END();

    TEST_CASE("N5.59 What The Setup Screen Draws For The Status Fits Its Box (A Review Fix): The Prompt Label Is 293 x 35, Two Lines Of 14 px: The Longest Typed Line (100 Wide Letters, 100 Narrow Ones, A Line Of Short Words), The \"You: ...\" Echo, A Received \"Name: ...\" And Any Other Text Never Needs A Third Line Or A Wider Line, With The Real Font; A Text That Is Cut Ends In \"...\", A Typed Line Shows Its End With The Prompt And The Caret; The Blink Does Not Move A Word") {
        Application app;
        ASSERT_TRUE(app.init(headless_config()));
        Renderer& r = app.renderer();
        using MS = MapSelectScreen;
        const auto fits = [&](const std::vector<std::string>& got) {
            if (got.size() > MS::STATUS_LINES) return false;
            for (const std::string& l : got) {
                if (r.get_text_width(l, FontSize::Px14) > MS::STATUS_W) return false;
            }
            return static_cast<int32_t>(got.size()) * font_cell_height(FontSize::Px14) <= MS::STATUS_H;
        };
        const std::vector<std::string> samples = {
            std::string(100, 'W'), std::string(100, 'i'), std::string(100, 'm'), std::string(100, '8'), std::string(100, '_'), std::string(100, '.'),
            "the quick brown fox jumps over the lazy dog and then does it again and again until the line has a hundred chars!!",
            "a b c d e f g h i j k l m n o p q r s t u v w x y z a b c d e f g h i j k l m n o p q r s t u v w x y z 0 1 2 3 4 5 6 7 8 9",
            "WWWW WWWW WWWW WWWW WWWW WWWW WWWW WWWW WWWW WWWW WWWW WWWW WWWW WWWW WWWW WWWW WWWW WWWW WWWW WWWW WWWW WWWW WWWW",
            "x", "", "supercalifragilisticexpialidocious-supercalifragilisticexpialidocious-supercalifragilisticexpialidocious",
        };
        for (const std::string& text : samples) {
            // a typed line: the prompt, what is typed (up to 100 characters), the caret on and off
            for (const bool caret : {true, false}) {
                MS::RoomView v;
                v.networked = true;
                v.status_input = true;
                v.status_prefix = "Say: ";
                v.status = text.substr(0, 100);
                v.status_caret = caret;
                const std::vector<std::string> got = MS::status_lines(r, v);
                ASSERT_TRUE(fits(got));
                ASSERT_FALSE(got.empty());
                std::string joined;
                for (const std::string& l : got) joined += l;
                ASSERT_TRUE(joined.find(caret ? "_" : " ") != std::string::npos);              // (the caret's place is in the last line, a blank when it blinks off)
                if (!text.empty()) ASSERT_TRUE(v.status.size() <= 100);
            }
            // the end of a long typed line is on the screen: its last characters are in the last line
            MS::RoomView typed;
            typed.networked = true;
            typed.status_input = true;
            typed.status_prefix = "Say: ";
            typed.status = text.substr(0, 100);
            typed.status_caret = true;
            const std::vector<std::string> typed_lines = MS::status_lines(r, typed);
            std::string shown_text;
            for (const std::string& l : typed_lines) shown_text += l;
            if (text.size() >= 8) ASSERT_TRUE(shown_text.find(text.substr(text.size() > 100 ? 100 - 4 : text.size() - 4, 4) + "_") != std::string::npos);        // the last characters of the line, and the caret behind them, are on the screen
            if (text.size() > 60) ASSERT_TRUE(shown_text.rfind("Say:", 0) != 0 || shown_text.size() > 20);                    // (a long line scrolls: the prompt may be gone, the end never is)
            // the echo of what this player said, and what somebody else said (the status text, cut by the net at 84 characters), and a long notice of any kind
            for (const std::string& shown : {"You: " + text.substr(0, 100), std::string("Name: ") + text.substr(0, 100), std::string("Bots cannot play with Fog of War. ") + text, text}) {
                MS::RoomView v;
                v.networked = true;
                v.status = shown;
                const std::vector<std::string> got = MS::status_lines(r, v);
                ASSERT_TRUE(fits(got));
            }
        }
        ASSERT_TRUE(r.get_text_width(fill_choice_caption(), FontSize::Px14) <= 350);                  // (the Host panel's caption fits its rectangle with the real font: 380 px wide now, and it is within the 350 px that it had)
        {   // the same texts as the net cuts them: 84 characters
            const std::string net_cut = "Ann: " + std::string(100, 'W');
            MS::RoomView v;
            v.networked = true;
            v.status = net_cut.substr(0, net::NetGame::kStatusNoticeChars - 3) + "...";
            ASSERT_TRUE(fits(MS::status_lines(r, v)));
        }
        {   // a text that is cut ends in "..." and keeps its beginning; one that fits is untouched (and is what the original's prompt was)
            MS::RoomView v;
            v.networked = true;
            v.status = "Bob: " + std::string(100, 'W');
            const std::vector<std::string> got = MS::status_lines(r, v);
            ASSERT_EQ(got.size(), size_t{2});
            ASSERT_TRUE(got[0].rfind("Bob:", 0) == 0);
            ASSERT_EQ(got[1].substr(got[1].size() - 3), std::string("..."));
            v.status = "Press START when all players' thumbs have appeared.";
            const std::vector<std::string> original = MS::status_lines(r, v);
            ASSERT_TRUE(!original.empty() && original.size() <= 2 && original == wrap_label_text(r, v.status, MS::STATUS_W, FontSize::Px14));
            v.status = "hi";
            ASSERT_EQ(MS::status_lines(r, v), (std::vector<std::string>{"hi"}));
            v.status.clear();
            ASSERT_EQ(MS::status_lines(r, v), original);                                       // an empty status is the original's prompt
        }
        {   // the blink moves nothing: the got are the same on and off, apart from the caret's own character
            MS::RoomView v;
            v.networked = true;
            v.status_input = true;
            v.status_prefix = "Say: ";
            for (const std::string& text : {std::string("hello"), std::string(37, 'a') + " " + std::string(31, 'b'), std::string(100, 'W'), std::string(76, 'x')}) {
                v.status = text;
                v.status_caret = true;
                const std::vector<std::string> on = MS::status_lines(r, v);
                v.status_caret = false;
                const std::vector<std::string> off = MS::status_lines(r, v);
                ASSERT_EQ(on.size(), off.size());
                for (size_t i = 0; i < on.size(); ++i) {
                    ASSERT_EQ(on[i].size(), off[i].size());
                    if (i + 1 < on.size()) ASSERT_EQ(on[i], off[i]);
                }
                ASSERT_TRUE(on.back().back() == '_' && off.back().back() == ' ' && on.back().substr(0, on.back().size() - 1) == off.back().substr(0, off.back().size() - 1));
            }
        }
        app.quit();
    } TEST_END();

    TEST_CASE("N5.76 The Refusal For Another Version On The Web Page Says To Reload The Page (A Tab That Was Opened Before The Server Was Updated Is The Old Game), In Words That Fit The Setup Screen's Status Box In Two Lines On Both Pages; The Desktop Game's Words Are As They Were, And Every Other Refusal Is The Same Words In Both") {
        using net::NetGame;
        using net::RejectReason;
        const std::string desktop = NetGame::reject_text(RejectReason::VersionMismatch, false);
        const std::string web = NetGame::reject_text(RejectReason::VersionMismatch, true);
        ASSERT_EQ(desktop, std::string("This version cannot play with the host's version."));            // (the words that test_netgame N3.17 pins for a native client)
        ASSERT_TRUE(web.compare(0, desktop.size(), desktop) == 0 && web.find("Reload the page") != std::string::npos);      // the same sentence, and what to do about it
        for (const RejectReason reason : {RejectReason::Full, RejectReason::MatchRunning, RejectReason::Kicked, RejectReason::BadRequest, RejectReason::NoSuchRoom, RejectReason::Dropped,
                                          RejectReason::RejoinFailed, RejectReason::Superseded}) {
            ASSERT_EQ(NetGame::reject_text(reason, false), NetGame::reject_text(reason, true));          // nothing else changes with the page
            ASSERT_TRUE(NetGame::reject_text(reason, true).find("Reload") == std::string::npos);
        }
        Application app;
        ASSERT_TRUE(app.init(headless_config()));
        Renderer& r = app.renderer();
        using MS = MapSelectScreen;
        MS::RoomView v;                                                                                   // the classic page (the web page's "Classic 4:3"): 293 px, two lines of 14 px
        v.networked = true;
        v.status = web;
        const std::vector<std::string> refusal_lines = MS::status_lines(r, v);
        ASSERT_TRUE(!refusal_lines.empty() && refusal_lines.size() <= MS::STATUS_LINES);
        std::string joined;
        for (const std::string& l : refusal_lines) {
            ASSERT_TRUE(r.get_text_width(l, FontSize::Px14) <= MS::STATUS_W);
            joined += (joined.empty() ? "" : " ") + l;
        }
        ASSERT_EQ(joined, web);                                                                           // not cut: no "..." and every word of it is on the screen
        const SetupLayout& wide = SetupLayout::of(SetupVariant::Guest);                                   // the 16:9 page (the web page's default): its label is wrapped at 363 px without a limit of lines
        ASSERT_TRUE(wrap_label_text(r, web, wide.prompt_text.w, FontSize::Px14).size() <= MS::STATUS_LINES);
        app.quit();
    } TEST_END();

    TEST_CASE("N5.76b The Refusal For A Room That The Server Cannot Make (A Code That Begins demo-, NoSuchRoom: The Cap Of Demo Rooms Is Full) Says So And What To Do, Not That There Is No Such Room, In Words That Fit The Setup Screen's Status Box In Two Lines On Both Pages; Any Other Code Is Told As Before") {
        using net::NetGame;
        using net::RejectReason;
        const std::string no_place = "The server cannot make a room for this match now. Try again in a few minutes.";
        const std::string no_room = "There is no such room on this server.";
        for (const bool browser : {false, true}) {
            ASSERT_EQ(NetGame::reject_text(RejectReason::NoSuchRoom, browser, "demo-treasure-4p-t01-k7m2xq"), no_place);
            ASSERT_EQ(NetGame::reject_text(RejectReason::NoSuchRoom, browser, "demo-x"), no_place);
            ASSERT_EQ(NetGame::reject_text(RejectReason::NoSuchRoom, browser, "demo-"), no_room);              // (a prefix alone is no code that a server makes a room of)
            ASSERT_EQ(NetGame::reject_text(RejectReason::NoSuchRoom, browser, "DEMO-1"), no_room);             // (the prefix is exactly demo-, as the server reads it)
            ASSERT_EQ(NetGame::reject_text(RejectReason::NoSuchRoom, browser, "ROOM-1"), no_room);
            ASSERT_EQ(NetGame::reject_text(RejectReason::NoSuchRoom, browser, ""), no_room);
            ASSERT_EQ(NetGame::reject_text(RejectReason::NoSuchRoom, browser), no_room);
            for (const RejectReason other : {RejectReason::Full, RejectReason::MatchRunning, RejectReason::Kicked, RejectReason::BadRequest, RejectReason::Dropped, RejectReason::RejoinFailed, RejectReason::Superseded}) {
                ASSERT_EQ(NetGame::reject_text(other, browser, "demo-treasure-4p-t01-k7m2xq"), NetGame::reject_text(other, browser));      // (nothing else changes with the code)
            }
        }
        Application app;
        ASSERT_TRUE(app.init(headless_config()));
        Renderer& r = app.renderer();
        using MS = MapSelectScreen;
        MS::RoomView v;                                                                                   // the classic page: 293 px, two lines of 14 px
        v.networked = true;
        v.status = no_place;
        const std::vector<std::string> place_lines = MS::status_lines(r, v);
        ASSERT_TRUE(!place_lines.empty() && place_lines.size() <= MS::STATUS_LINES);
        std::string joined;
        for (const std::string& l : place_lines) {
            ASSERT_TRUE(r.get_text_width(l, FontSize::Px14) <= MS::STATUS_W);
            joined += (joined.empty() ? "" : " ") + l;
        }
        ASSERT_EQ(joined, no_place);                                                                      // not cut: no "..." and every word of it is on the screen
        const SetupLayout& wide = SetupLayout::of(SetupVariant::Guest);                                   // the 16:9 page
        ASSERT_TRUE(wrap_label_text(r, no_place, wide.prompt_text.w, FontSize::Px14).size() <= MS::STATUS_LINES);
        app.quit();
    } TEST_END();
}

// ---- the chat box of the 16:9 setup screen (online rooms, protocol 11): the application feeds MapSelectScreen::set_chat_panel / set_fill_footer from the room (N5.60 - N5.68) ----
void run_room_chat_box_tests() {
    using SL = SetupLayout;
    const auto centre_x = [](const LayoutRect& r) { return r.x + r.w / 2; };
    const auto centre_y = [](const LayoutRect& r) { return r.y + r.h / 2; };
    // The application of a test in the 16:9 picture (960 x 540, its window the same size so that the pixels are the picture's: a frame is run to be read back, so the headless
    // application is told to take a screenshot that never comes instead of stopping after ten frames) or in the original's 4:3 one
    const auto room_config = [](const Server& server, const std::string& room, const std::string& name, Aspect aspect, net::FillLevel fill, float zoom_level = 1.0f, bool zoom_given = false) {
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Join;
        cfg.net_address = "127.0.0.1";
        cfg.net_port = server.port();
        cfg.net_room = room;
        cfg.player_name = name;
        cfg.aspect = aspect;
        cfg.aspect_given = true;
        cfg.fill_bots = fill;
        cfg.zoom = zoom_level;                                                                // (the remembered level of the mouse-wheel zoom: the meeting of the rooms and the zoom, N5.68 - N5.71)
        cfg.zoom_given = zoom_given;
        cfg.screenshot_path = (std::filesystem::temp_directory_path() / ("ants_box_" + room + ".png")).string();
        cfg.screenshot_frames = 1000000000;
        if (aspect == Aspect::Wide16x9) {
            cfg.has_window_size = true;
            cfg.window_w = 960;
            cfg.window_h = 540;
        }
        return cfg;
    };
    // the application is the room's leader (the first to join), Bob a bare machine who joins second; the room is TINY, 4 seats, with Fog of War when `fog`
    struct BoxRoom {
        Server server;
        Application app;
        Peer peer;
        Hall hall{server, &app, {&peer}};
    };
    const auto open_leader = [&](BoxRoom& d, const std::string& code, Aspect aspect, net::FillLevel fill, bool fog, bool with_bob, float zoom_level = 1.0f, bool zoom_given = false) -> bool {
        server::RoomSpec spec;
        spec.code = code;
        spec.map = "TINY.LVL";
        spec.players = 4;
        spec.fog = fog;
        spec.has_seed = true;
        spec.seed = 4242;
        if (d.server.listener == nullptr || !d.server.mgr.create_room(spec, d.server.now).ok) return false;
        if (!d.app.init(room_config(d.server, code, "Ana", aspect, fill, zoom_level, zoom_given))) return false;
        if (!d.hall.until([&]() { return d.app.net()->phase() == net::NetGame::Phase::Room && d.app.net()->is_leader(); }, 8000)) return false;
        if (with_bob) {
            if (!d.peer.net.join("127.0.0.1", d.server.port(), "Bob", 255, code)) return false;
            if (!d.hall.until([&]() { return d.peer.net.phase() == net::NetGame::Phase::Room && d.app.net()->room().slots[1].rtt_ms != net::kRttUnknown; }, 8000)) return false;
        }
        d.hall.step(700);                                                                    // (the setup screen has shown its labels: the refresh of 500 ms)
        return true;
    };
    // the application is a GUEST: a bare machine (named Lea) joined first and leads the room, the application joins second
    const auto open_guest = [&](BoxRoom& d, const std::string& code, Aspect aspect, net::FillLevel fill, float zoom_level = 1.0f, bool zoom_given = false) -> bool {
        server::RoomSpec spec;
        spec.code = code;
        spec.map = "TINY.LVL";
        spec.players = 4;
        spec.has_seed = true;
        spec.seed = 4242;
        if (d.server.listener == nullptr || !d.server.mgr.create_room(spec, d.server.now).ok) return false;
        Hall before{d.server, nullptr, {&d.peer}};
        if (!d.peer.net.join("127.0.0.1", d.server.port(), "Lea", 255, code)) return false;
        if (!before.until([&]() { return d.peer.net.phase() == net::NetGame::Phase::Room; }, 8000)) return false;
        if (!d.app.init(room_config(d.server, code, "Ben", aspect, fill, zoom_level, zoom_given))) return false;
        if (!d.hall.until([&]() { return d.app.net()->phase() == net::NetGame::Phase::Room && d.app.net()->my_seat() == 1 && d.peer.net.room().slots[1].rtt_ms != net::kRttUnknown; }, 8000)) return false;
        d.hall.step(700);
        return true;
    };
    const auto panel_lines = [](Application& app) {
        std::vector<std::string> out;
        for (const MapSelectScreen::ChatPanel::Line& l : app.map_select().chat_panel().lines) out.push_back((l.notice ? "! " : "") + l.text);
        return out;
    };
    // the pixels of a rectangle of the picture (the renderer's canvas is the picture: the window is 960 x 540), as a hash
    const auto region_hash = [](Application& app, const LayoutRect& r) {
        SDL_Rect rect{r.x, r.y, r.w, r.h};
        std::vector<uint8_t> px(static_cast<size_t>(r.w) * static_cast<size_t>(r.h) * 4u, 0);
        if (SDL_RenderReadPixels(app.renderer().get_sdl_renderer(), &rect, SDL_PIXELFORMAT_RGBA32, px.data(), r.w * 4) != 0) return uint64_t{0};
        uint64_t h = 1469598103934665603ull;
        for (const uint8_t b : px) h = (h ^ b) * 1099511628211ull;
        return h;
    };
    const auto lines = [](const std::vector<net::ChatLine>& v) {
        std::vector<std::string> out;
        for (const net::ChatLine& l : v) out.push_back(std::to_string(static_cast<unsigned>(l.seat)) + "|" + l.name + "|" + l.text);
        return out;
    };

    TEST_CASE("N5.60 The 16:9 Setup Screen Of A Room's Leader Has A Chat Box Fed From The Room: The Lines With Their Names Arrive In It (Own Line Included, Oldest First, The Newest 16 At Most), The Typed Line And Its Caret Are In The Input Box, The Status Line Keeps The Prompt (No Copy Of The Chat, No \"Say:\", No \"You:\")") {
        BoxRoom d;
        ASSERT_TRUE(open_leader(d, "BOX-LEAD", Aspect::Wide16x9, net::FillLevel::None, false, true));
        Application& app = d.app;
        ASSERT_TRUE(app.map_select().wide_layout() && app.map_select().setup_variant() == SetupVariant::Online);
        ASSERT_TRUE(app.room_chat_box() == &SL::of(SetupVariant::Online).chat);
        ASSERT_TRUE(app.map_select().chat_panel().visible);                                     // (the box is drawn from the first frame of the room: its frame and label)
        ASSERT_TRUE(app.map_select().chat_panel().lines.empty() && app.map_select().chat_panel().typed.empty() && !app.map_select().chat_panel().caret);
        d.hall.step(5000);                                                                      // (the first five seconds of company say how to change a colour: the START prompt is back after them)
        const std::string prompt = app.map_select().room().status;
        ASSERT_EQ(prompt, std::string(sim::strings::text(sim::strings::kPressStart)));
        // Bob speaks: the line is in the box with his name; the status line is not changed by it
        ASSERT_TRUE(d.peer.net.chat("hello leader"));
        ASSERT_TRUE(d.hall.until([&]() { return app.map_select().chat_panel().lines.size() == 1; }, 8000));
        ASSERT_EQ(panel_lines(app), (std::vector<std::string>{"Bob: hello leader"}));
        d.hall.step(30);
        ASSERT_EQ(app.map_select().room().status, prompt);
        ASSERT_EQ(app.net()->status_text(), prompt);
        ASSERT_FALSE(app.map_select().room().status_input);
        // T opens the input: what is typed is in the box (not on the status line), the caret blinks
        app.room_key_down(SDLK_t, 0, false);
        app.room_text_input("t");
        app.room_text_input("my reply");
        d.hall.step(30);
        ASSERT_TRUE(app.map_select().chat_panel().typed == "my reply" && app.room_chat().is_open());
        ASSERT_TRUE(app.map_select().room().status == prompt && !app.map_select().room().status_input && app.map_select().room().status_prefix.empty());
        bool saw_on = false;
        bool saw_off = false;
        for (int i = 0; i < 80; ++i) {                                                           // (the caret's half period is 150 ms: in 800 ms it is seen on and off)
            d.hall.step(10);
            (app.map_select().chat_panel().caret ? saw_on : saw_off) = true;
            ASSERT_EQ(app.map_select().chat_panel().caret, app.room_chat().caret(static_cast<uint32_t>(app.net_clock_ms())));
        }
        ASSERT_TRUE(saw_on && saw_off);
        // Enter sends it: the room relays it back to its sender too, so it comes into the box with the sender's own name; the input is empty again; "You: ..." is nowhere
        app.room_key_down(SDLK_RETURN, 0, false);
        ASSERT_TRUE(d.hall.until([&]() { return app.map_select().chat_panel().lines.size() == 2; }, 8000));
        ASSERT_EQ(panel_lines(app), (std::vector<std::string>{"Bob: hello leader", "Ana: my reply"}));
        d.hall.step(30);
        ASSERT_TRUE(app.map_select().chat_panel().typed.empty() && !app.map_select().chat_panel().caret && !app.room_chat().is_open());
        ASSERT_TRUE(app.map_select().room().status == prompt && app.net()->status_text().find("You:") == std::string::npos);
        ASSERT_EQ(lines(d.peer.net.pregame_chat()), (std::vector<std::string>{"1|Bob|hello leader", "0|Ana|my reply"}));
        // the room's talk goes on: only the newest 16 lines are given to the box, the newest last (the box wraps what it gets and shows what fits)
        for (int i = 1; i <= 20; ++i) {
            ASSERT_TRUE(d.peer.net.chat("line " + std::to_string(i)));
            d.hall.step(1100);                                                                  // (a player may say a line a second: the room's own rate limit)
        }
        ASSERT_TRUE(d.hall.until([&]() { return app.net()->pregame_chat().size() == 22; }, 8000));
        d.hall.step(30);
        const std::vector<std::string> shown = panel_lines(app);
        ASSERT_EQ(shown.size(), size_t{16});
        ASSERT_EQ(shown.front(), std::string("Bob: line 5"));
        ASSERT_EQ(shown.back(), std::string("Bob: line 20"));
        ASSERT_EQ(app.net()->pregame_chat().size(), size_t{22});                                // (the room keeps them all: the match's chat log starts with them)
        ASSERT_TRUE(app.map_select().room().status == prompt);
        app.quit();
    } TEST_END();

    TEST_CASE("N5.61 The Chat Box Styles A Room's Own Notice As A Notice (A Line Of The Server, Not Of A Player: No Name In Front, The Status Text's Smaller Size) And A Player's Line As A Message; The Status Line Has The Prompt Of The Fog Room, Not A Copy Of The Notice") {
        BoxRoom d;
        ASSERT_TRUE(open_leader(d, "BOX-NOTE", Aspect::Wide16x9, net::FillLevel::Easy, true, false));
        Application& app = d.app;
        ASSERT_TRUE(app.map_select().chat_panel().visible && app.map_select().chat_panel().lines.empty());
        ASSERT_EQ(app.map_select().room().status, std::string("Fog of War is on, so START seats no bots."));
        app.room_key_down(SDLK_s, 0, false);                                                    // START of a leader alone with a fill in a fog room: the server says that bots cannot play with fog
        ASSERT_TRUE(d.hall.until([&]() { return app.map_select().chat_panel().lines.size() == 1; }, 8000));
        ASSERT_EQ(panel_lines(app), (std::vector<std::string>{std::string("! ") + net::kNoticeFillFog}));
        ASSERT_TRUE(app.map_select().chat_panel().lines[0].notice);
        d.hall.step(30);
        ASSERT_EQ(app.map_select().room().status, std::string("Fog of War is on, so START seats no bots."));      // (no copy of the notice on the status line)
        ASSERT_TRUE(d.peer.net.join("127.0.0.1", d.server.port(), "Bob", 255, "BOX-NOTE"));
        ASSERT_TRUE(d.hall.until([&]() { return d.peer.net.phase() == net::NetGame::Phase::Room; }, 8000));
        ASSERT_TRUE(d.peer.net.chat("hi"));
        ASSERT_TRUE(d.hall.until([&]() { return app.map_select().chat_panel().lines.size() == 2; }, 8000));
        ASSERT_EQ(panel_lines(app), (std::vector<std::string>{std::string("! ") + net::kNoticeFillFog, "Bob: hi"}));
        ASSERT_TRUE(app.map_select().chat_panel().lines[0].notice && !app.map_select().chat_panel().lines[1].notice);
        app.quit();
    } TEST_END();

    TEST_CASE("N5.62 The Click Zones Of The 16:9 Screen: A Click In The Chat Box's Input Box Opens The Chat Input And So Does One In Its Lines Box, A Second Click Closes It (Sends Nothing), Any Other Click While It Is Open Closes It And Is Not The Screen's Click (START Starts Nothing); The Classic Page's Status-Line Zone And The Wide Status Box Open Nothing") {
        BoxRoom d;
        ASSERT_TRUE(open_leader(d, "BOX-CLICK", Aspect::Wide16x9, net::FillLevel::None, false, true));
        Application& app = d.app;
        const SetupLayout& layout = SL::of(SetupVariant::Online);
        const LayoutRect input = layout.chat.input_box;
        const LayoutRect lines_box = layout.chat.lines;
        const auto quiet = [&]() { return d.server.status("BOX-CLICK").state == server::RoomState::Waiting && !app.map_select().is_locked() && app.state() == AppState::MapSelect; };
        const auto click = [&](int32_t x, int32_t y) { return app.room_mouse_down(x, y, SDL_BUTTON_LEFT); };
        // a click that is nobody's: just outside the input box on every side, just outside the lines box, between them, the classic status line's zone, the wide status box, the preview, the button
        for (const auto& p : {std::pair<int32_t, int32_t>{input.x - 1, centre_y(input)}, {input.x + input.w, centre_y(input)}, {centre_x(input), input.y - 1}, {centre_x(input), input.y + input.h},
                              {lines_box.x - 1, centre_y(lines_box)}, {lines_box.x + lines_box.w, centre_y(lines_box)}, {centre_x(lines_box), lines_box.y - 1},
                              {MapSelectScreen::LABEL_X + 5, MapSelectScreen::STATUS_Y + 5}, {centre_x(layout.prompt_text), centre_y(layout.prompt_text)},
                              {centre_x(layout.preview_box), centre_y(layout.preview_box)}, {centre_x(layout.start), centre_y(layout.start)}}) {
            ASSERT_FALSE(click(p.first, p.second));
            ASSERT_FALSE(app.room_chat().is_open());
        }
        ASSERT_FALSE(app.room_mouse_down(centre_x(input), centre_y(input), SDL_BUTTON_RIGHT));     // the right button is no click
        ASSERT_FALSE(app.room_chat().is_open());
        // the input box opens it; the release of that click is the input's
        ASSERT_TRUE(click(centre_x(input), centre_y(input)));
        ASSERT_TRUE(app.room_chat().is_open());
        ASSERT_TRUE(app.room_mouse_up(centre_x(input), centre_y(input), SDL_BUTTON_LEFT));
        app.room_text_input("typed with a finger");
        d.hall.step(30);
        ASSERT_EQ(app.map_select().chat_panel().typed, std::string("typed with a finger"));
        // a second click on the input box closes it and sends nothing
        ASSERT_TRUE(click(centre_x(input), centre_y(input)));
        ASSERT_FALSE(app.room_chat().is_open());
        ASSERT_TRUE(app.room_mouse_up(centre_x(input), centre_y(input), SDL_BUTTON_LEFT));
        d.hall.step(500);
        ASSERT_TRUE(d.peer.net.pregame_chat().empty() && quiet());
        ASSERT_TRUE(app.map_select().chat_panel().typed.empty());
        // the corners of both boxes are inside them
        for (const LayoutRect& r : {input, lines_box}) {
            for (const auto& p : {std::pair<int32_t, int32_t>{r.x, r.y}, {r.x + r.w - 1, r.y}, {r.x, r.y + r.h - 1}, {r.x + r.w - 1, r.y + r.h - 1}}) {
                ASSERT_TRUE(click(p.first, p.second));
                ASSERT_TRUE(app.room_chat().is_open());
                ASSERT_TRUE(app.room_mouse_up(p.first, p.second, SDL_BUTTON_LEFT));
                ASSERT_TRUE(click(p.first, p.second));                                            // (and closes it again)
                ASSERT_FALSE(app.room_chat().is_open());
                ASSERT_TRUE(app.room_mouse_up(p.first, p.second, SDL_BUTTON_LEFT));
            }
        }
        // a click in the lines box opens it too
        ASSERT_TRUE(click(centre_x(lines_box), centre_y(lines_box)));
        ASSERT_TRUE(app.room_chat().is_open());
        ASSERT_TRUE(app.room_mouse_up(centre_x(lines_box), centre_y(lines_box), SDL_BUTTON_LEFT));
        ASSERT_TRUE(click(centre_x(lines_box), centre_y(lines_box)));                              // (a second click anywhere closes it)
        ASSERT_FALSE(app.room_chat().is_open());
        ASSERT_TRUE(app.room_mouse_up(centre_x(lines_box), centre_y(lines_box), SDL_BUTTON_LEFT));
        // any other click while it is open closes it and is not the screen's: START (and the map list, the classic status zone, the empty clay) start and change nothing
        const int32_t map_before = app.map_select().get_selected_index();
        for (const auto& where : {std::pair<int32_t, int32_t>{centre_x(layout.start), centre_y(layout.start)}, {centre_x(layout.down), centre_y(layout.down)}, {centre_x(layout.prompt_text), centre_y(layout.prompt_text)}, {20, 200}}) {
            ASSERT_TRUE(click(centre_x(input), centre_y(input)));
            ASSERT_TRUE(app.room_chat().is_open());
            ASSERT_TRUE(app.room_mouse_up(centre_x(input), centre_y(input), SDL_BUTTON_LEFT));
            app.room_text_input("words");
            ASSERT_TRUE(click(where.first, where.second));
            ASSERT_FALSE(app.room_chat().is_open());
            ASSERT_FALSE(app.map_select().start_button().pressed());
            ASSERT_TRUE(app.room_mouse_up(where.first, where.second, SDL_BUTTON_LEFT));
            d.hall.step(300);
            ASSERT_TRUE(quiet() && d.peer.net.pregame_chat().empty());
        }
        ASSERT_EQ(app.map_select().get_selected_index(), map_before);
        app.quit();
    } TEST_END();

    TEST_CASE("N5.63 The Focus Rule On The 16:9 Screen: While The Chat Input Is Open S, Q, X, Enter And The Arrows Do Nothing On The Screen And Are Not Typed (The Box Shows Exactly What Was Typed), Enter Sends And Closes, Esc Closes; After It Closes START's Keys Do Nothing For 400 ms; A Held START Key Starts Nothing On A Room's Screen") {
        BoxRoom d;
        ASSERT_TRUE(open_leader(d, "BOX-FOCUS", Aspect::Wide16x9, net::FillLevel::None, false, true));
        Application& app = d.app;
        const auto quiet = [&]() { return d.server.status("BOX-FOCUS").state == server::RoomState::Waiting && !app.map_select().is_locked() && app.state() == AppState::MapSelect && app.network_active(); };
        // the keys that must not open it
        app.room_key_down(SDLK_t, KMOD_CTRL, false);
        app.room_key_down(SDLK_t, KMOD_GUI, false);
        app.room_key_down(SDLK_t, 0, true);
        ASSERT_FALSE(app.room_chat().is_open());
        // T opens it and is not typed; the keys of the screen do nothing and do not enter the line
        app.room_key_down(SDLK_t, 0, false);
        ASSERT_TRUE(app.room_chat().is_open());
        app.room_text_input("t");
        app.room_text_input("hello");
        for (const SDL_Keycode k : {SDLK_s, SDLK_q, SDLK_x, SDLK_UP, SDLK_DOWN, SDLK_LEFT, SDLK_RIGHT, SDLK_TAB}) app.room_key_down(k, 0, false);
        d.hall.step(600);
        ASSERT_TRUE(quiet() && app.room_chat().is_open());
        ASSERT_EQ(app.room_chat().text(), std::string("hello"));
        ASSERT_EQ(app.map_select().chat_panel().typed, std::string("hello"));
        // Esc closes it without sending anything; the box is empty again
        app.room_key_down(SDLK_ESCAPE, 0, false);
        d.hall.step(30);
        ASSERT_TRUE(!app.room_chat().is_open() && app.map_select().chat_panel().typed.empty() && d.peer.net.pregame_chat().empty());
        // Enter sends and closes; a second Enter (and the S of a habit) within 400 ms starts nothing
        d.hall.step(500);
        app.room_key_down(SDLK_t, 0, false);
        app.room_text_input("t");
        app.room_text_input("ready");
        app.room_key_down(SDLK_RETURN, 0, false);
        ASSERT_FALSE(app.room_chat().is_open());
        d.hall.step(20);
        app.room_key_down(SDLK_RETURN, 0, false);
        app.room_key_down(SDLK_s, 0, false);
        d.hall.step(300);
        ASSERT_TRUE(quiet());
        ASSERT_TRUE(d.hall.until([&]() { return !d.peer.net.pregame_chat().empty(); }, 8000));
        ASSERT_EQ(lines(d.peer.net.pregame_chat()), (std::vector<std::string>{"0|Ana|ready"}));
        // a held START key (the auto-repeat of Enter, the keypad's Enter, S) is no press on a room's screen, however long it has been held
        d.hall.step(500);
        for (const SDL_Keycode k : {SDLK_RETURN, SDLK_KP_ENTER, SDLK_s}) {
            for (int i = 0; i < 5; ++i) app.room_key_down(k, 0, true);
        }
        d.hall.step(1500);
        ASSERT_TRUE(quiet());
        // and the press of S, 400 ms or more after the input closed, starts the match
        app.room_key_down(SDLK_s, 0, false);
        ASSERT_TRUE(d.hall.until([&]() { return app.state() == AppState::Playing && d.peer.net.phase() == net::NetGame::Phase::Playing; }, 15000));
        app.quit();
    } TEST_END();

    TEST_CASE("N5.64 The Fill Footer Of The Leader's Players' Status Box (\"Empty seats at START:\" / \"Medium bots\"): The Leader Sees The Level Of Its Fill (Follows A Change At Once), A Room Without A Fill, A Fog Room (No Bots Can Be Seated), A Guest Whatever Its Own Setting, And The Classic Page Have None") {
        const auto footer = [](Application& app) { return std::vector<std::string>{app.map_select().fill_footer()[0], app.map_select().fill_footer()[1]}; };
        const std::vector<std::string> none{"", ""};
        {
            BoxRoom d;
            ASSERT_TRUE(open_leader(d, "BOX-FILL", Aspect::Wide16x9, net::FillLevel::Medium, false, true));
            Application& app = d.app;
            ASSERT_EQ(footer(app), (std::vector<std::string>{"Empty seats at START:", "Medium bots"}));
            app.set_fill_bots(net::FillLevel::Easy);
            d.hall.step(30);
            ASSERT_EQ(footer(app), (std::vector<std::string>{"Empty seats at START:", "Easy bots"}));
            app.set_fill_bots(net::FillLevel::Hard);
            d.hall.step(30);
            ASSERT_EQ(footer(app), (std::vector<std::string>{"Empty seats at START:", "Hard bots"}));
            app.set_fill_bots(net::FillLevel::None);
            d.hall.step(30);
            ASSERT_EQ(footer(app), none);
            app.quit();
        }
        {   // a fog room seats no bots: the footer would promise what START does not do
            BoxRoom d;
            ASSERT_TRUE(open_leader(d, "BOX-FILL-FOG", Aspect::Wide16x9, net::FillLevel::Medium, true, true));
            ASSERT_EQ(footer(d.app), none);
            d.app.quit();
        }
        {   // a guest cannot START: nothing, whatever its own --fill-bots says
            BoxRoom d;
            ASSERT_TRUE(open_guest(d, "BOX-FILL-GUEST", Aspect::Wide16x9, net::FillLevel::Medium));
            ASSERT_EQ(d.app.fill_bots(), net::FillLevel::Medium);
            ASSERT_EQ(footer(d.app), none);
            d.app.quit();
        }
        {   // the classic page has no chat box and no footer
            BoxRoom d;
            ASSERT_TRUE(open_leader(d, "BOX-FILL-CLASSIC", Aspect::Classic4x3, net::FillLevel::Medium, false, true));
            ASSERT_EQ(footer(d.app), none);
            d.app.quit();
        }
    } TEST_END();

    TEST_CASE("N5.82 Protocol 13, The Room's Teams On The Leader's 16:9 Screen: The Foot Of The Players' Status Box Says \"Room teams:\" And The Pair When The Room's Code Names Them (Whatever The Leader's Own --teams Is), \"Teams at START:\" For The Leader's Own Choice In A Room Whose Code Names None; A Room That Reads No Code (Made By The Control Interface) Still Gets The Code's Teams, Because The Leader's START Carries The Effective Teams") {
        using L = net::FillLevel;
        const auto footer = [](Application& app) { return std::vector<std::string>{app.map_select().fill_footer()[0], app.map_select().fill_footer()[1]}; };
        const net::FillPlan blue_easy_black_hard(std::array<L, 4>{L::None, L::None, L::Easy, L::Hard});
        {   // the room's code names Green + Red: the leader and Bob are in, no bots yet
            BoxRoom d;
            ASSERT_TRUE(open_leader(d, "demo-tiny-4p-t01-box123", Aspect::Wide16x9, L::None, false, true));
            Application& app = d.app;
            ASSERT_TRUE(app.net()->room_teams() == sim::StartTeams({true, 0, 1}));
            ASSERT_EQ(footer(app), (std::vector<std::string>{"Room teams:", "Green + Red"}));          // (the seats that play so far: the pair alone)
            app.set_start_teams(LocalTeams{true, 1, 2});                                               // the leader's own choice: the room's code wins, nothing of it is shown
            d.hall.step(30);
            ASSERT_EQ(footer(app), (std::vector<std::string>{"Room teams:", "Green + Red"}));
            ASSERT_TRUE(app.net()->start_teams() == sim::StartTeams({true, 1, 2}) && app.net()->effective_teams() == sim::StartTeams({true, 0, 1}));
            app.set_fill_bots(blue_easy_black_hard);
            d.hall.step(30);
            const std::vector<std::string> both = footer(app);                                        // bots and teams: the bots in the first line, the room's teams in the second
            ASSERT_EQ(both, (std::vector<std::string>{"Bots: Blue Easy, Black Hard", "Room teams: Green + Red vs Blue + Black"}));      // (the longest way that fits the box at 12 px with the real font: "against" does not)
            app.quit();
        }
        {   // a room whose code names none: the leader's own choice, said as a choice of START, as before
            BoxRoom d;
            ASSERT_TRUE(open_leader(d, "demo-tiny-4p-box456", Aspect::Wide16x9, L::None, false, true));
            Application& app = d.app;
            ASSERT_FALSE(app.net()->room_teams().set);
            ASSERT_EQ(footer(app), (std::vector<std::string>{"", ""}));
            app.set_start_teams(LocalTeams{true, 1, 2});
            d.hall.step(30);
            ASSERT_EQ(footer(app), (std::vector<std::string>{"Teams at START:", "Red + Blue"}));
            app.quit();
        }
        {   // a room that reads no code (the control interface made it, as these rooms are made): its code names Green + Red, the leader's START carries them and the room has none of its own
            BoxRoom d;
            ASSERT_TRUE(open_leader(d, "demo-tiny-4p-t01-box789", Aspect::Wide16x9, L::None, false, true));
            Application& app = d.app;
            ASSERT_TRUE(d.server.status("demo-tiny-4p-t01-box789").room_teams.empty());
            app.set_start_teams(LocalTeams{true, 1, 2});                                               // (its own choice: not what the request carries)
            app.set_fill_bots(blue_easy_black_hard);
            d.hall.step(30);
            app.room_key_down(SDLK_s, 0, false);
            ASSERT_TRUE(d.hall.until([&]() { return app.state() == AppState::Playing && d.peer.net.phase() == net::NetGame::Phase::Playing; }, 15000));
            const server::RoomStatus s = d.server.status("demo-tiny-4p-t01-box789");
            ASSERT_TRUE(s.state == server::RoomState::Running && s.joined == 4 && s.bots.size() == 2 && s.room_teams.empty());
            ASSERT_EQ(s.teams, std::string("0+1"));                                                    // the effective teams, not the leader's own 1 + 2
            ASSERT_TRUE(s.allies[0] == 1 && s.allies[1] == 0 && s.allies[2] == 3 && s.allies[3] == 2);
            ASSERT_TRUE(app.sim().alliance_of(0) == 1 && app.sim().alliance_of(2) == 3 && d.peer.sim.alliance_of(1) == 0);
            app.quit();
        }
    } TEST_END();

    TEST_CASE("N5.65 A Guest's 16:9 Screen Has The Same Chat Box (The Guest Layout's): The Lines Of The Room Arrive In It, A Click In Its Input Box Or Its Lines And T Open The Input, The Line Reaches The Room, The Status Line Keeps \"Waiting For The Host To Start The Game...\"; The Guest's Keys Start Nothing") {
        BoxRoom d;
        ASSERT_TRUE(open_guest(d, "BOX-GUEST", Aspect::Wide16x9, net::FillLevel::None));
        Application& app = d.app;
        ASSERT_TRUE(app.map_select().setup_variant() == SetupVariant::Guest && app.map_select().is_guest());
        ASSERT_TRUE(app.room_chat_box() == &SL::of(SetupVariant::Guest).chat);
        ASSERT_TRUE(app.map_select().chat_panel().visible && app.map_select().chat_panel().lines.empty());
        const std::string prompt = std::string(sim::strings::text(sim::strings::kWaitingForHost));
        ASSERT_EQ(app.map_select().room().status, prompt);
        ASSERT_TRUE(d.peer.net.chat("welcome, Ben"));
        ASSERT_TRUE(d.hall.until([&]() { return app.map_select().chat_panel().lines.size() == 1; }, 8000));
        ASSERT_EQ(panel_lines(app), (std::vector<std::string>{"Lea: welcome, Ben"}));
        d.hall.step(30);
        ASSERT_EQ(app.map_select().room().status, prompt);
        // the clicks: the Guest layout's boxes
        const SetupChatLayout& chat = SL::of(SetupVariant::Guest).chat;
        ASSERT_FALSE(app.room_mouse_down(chat.input_box.x - 1, centre_y(chat.input_box), SDL_BUTTON_LEFT));
        ASSERT_TRUE(app.room_mouse_down(centre_x(chat.input_box), centre_y(chat.input_box), SDL_BUTTON_LEFT));
        ASSERT_TRUE(app.room_chat().is_open());
        ASSERT_TRUE(app.room_mouse_up(centre_x(chat.input_box), centre_y(chat.input_box), SDL_BUTTON_LEFT));
        app.room_text_input("thanks");
        d.hall.step(30);
        ASSERT_EQ(app.map_select().chat_panel().typed, std::string("thanks"));
        for (const SDL_Keycode k : {SDLK_s, SDLK_q, SDLK_x}) app.room_key_down(k, 0, false);        // (a guest has no START, and Leave is not for a line that is typed)
        d.hall.step(30);
        ASSERT_TRUE(app.room_chat().is_open() && app.network_active() && app.map_select().chat_panel().typed == "thanks");
        app.room_key_down(SDLK_RETURN, 0, false);
        ASSERT_TRUE(d.hall.until([&]() { return app.map_select().chat_panel().lines.size() == 2; }, 8000));
        ASSERT_EQ(panel_lines(app), (std::vector<std::string>{"Lea: welcome, Ben", "Ben: thanks"}));
        ASSERT_EQ(lines(d.peer.net.pregame_chat()), (std::vector<std::string>{"0|Lea|welcome, Ben", "1|Ben|thanks"}));
        ASSERT_EQ(app.map_select().room().status, prompt);
        // T opens it for a guest too
        d.hall.step(500);
        app.room_key_down(SDLK_t, 0, false);
        ASSERT_TRUE(app.room_chat().is_open());
        app.room_key_down(SDLK_ESCAPE, 0, false);
        d.hall.step(450);
        for (const SDL_Keycode k : {SDLK_s, SDLK_RETURN}) app.room_key_down(k, 0, false);
        d.hall.step(1000);
        ASSERT_TRUE(d.server.status("BOX-GUEST").state == server::RoomState::Waiting && app.state() == AppState::MapSelect);
        app.quit();
    } TEST_END();

    TEST_CASE("N5.66 The Classic 640 x 480 Setup Screen Is Unchanged: No Chat Box (None Drawn, None Fed, No Fill Footer, No Box Zones), The Status Line Shows The Room's Lines For Five Seconds, \"You: ...\" After A Line Is Said, \"Say: ...\" While It Is Typed, And A Click On It Opens The Input") {
        BoxRoom d;
        ASSERT_TRUE(open_leader(d, "BOX-CLASSIC", Aspect::Classic4x3, net::FillLevel::Medium, false, true));
        Application& app = d.app;
        ASSERT_FALSE(app.map_select().wide_layout());
        ASSERT_TRUE(app.room_chat_box() == nullptr);
        ASSERT_FALSE(app.map_select().chat_panel().visible);
        ASSERT_TRUE(d.peer.net.chat("hello classic"));
        ASSERT_TRUE(d.hall.until([&]() { return !app.net()->pregame_chat().empty(); }, 8000));
        d.hall.step(30);
        ASSERT_EQ(app.map_select().room().status, std::string("Bob: hello classic"));            // the mirror of the chat on the status line
        ASSERT_TRUE(app.map_select().chat_panel().lines.empty() && !app.map_select().chat_panel().visible);
        // the wide screen's zones are nothing here (a click at the wide input box's place: the classic page has the map's box and the players there)
        const SetupChatLayout& wide = SL::of(SetupVariant::Online).chat;
        ASSERT_FALSE(app.room_mouse_down(std::min(centre_x(wide.input_box), 600), std::min(centre_y(wide.input_box), 470), SDL_BUTTON_LEFT));
        ASSERT_FALSE(app.room_chat().is_open());
        // typing: the prompt "Say: ...", the caret; Enter: "You: ..."
        ASSERT_TRUE(app.room_mouse_down(MapSelectScreen::LABEL_X + 5, MapSelectScreen::STATUS_Y + 5, SDL_BUTTON_LEFT));
        ASSERT_TRUE(app.room_chat().is_open());
        ASSERT_TRUE(app.room_mouse_up(MapSelectScreen::LABEL_X + 5, MapSelectScreen::STATUS_Y + 5, SDL_BUTTON_LEFT));
        app.room_text_input("on the status line");
        d.hall.step(30);
        ASSERT_TRUE(app.map_select().room().status_input && app.map_select().room().status_prefix == "Say: " && app.map_select().room().status == "on the status line");
        ASSERT_TRUE(app.map_select().chat_panel().typed.empty() && app.map_select().chat_panel().lines.empty());
        app.room_key_down(SDLK_RETURN, 0, false);
        d.hall.step(30);
        ASSERT_EQ(app.map_select().room().status, std::string("You: on the status line"));
        ASSERT_TRUE(app.map_select().fill_footer()[0].empty() && app.map_select().fill_footer()[1].empty());     // (the level of the fill is in the status line's own prompt there)
        d.hall.step(6000);
        ASSERT_EQ(app.map_select().room().status, std::string("Press START: the empty seats get Medium bots."));
        app.quit();
    } TEST_END();

    TEST_CASE("N5.67 Through The Window's Own Event Loop On The 16:9 Screen: T, The Typed Letters, A Click On The Input Box (Motion, Press, Release As SDL Queues Them) And Enter Reach The Chat Input; The Picture Of The Box Changes With The Lines, The Typed Text And The Caret, And Nothing Else Of The Setup Screen's Chat Column Moves") {
        BoxRoom d;
        ASSERT_TRUE(open_leader(d, "BOX-EVENTS", Aspect::Wide16x9, net::FillLevel::None, false, true));
        Application& app = d.app;
        const SetupChatLayout& chat = SL::of(SetupVariant::Online).chat;
        const auto push = [](SDL_Event e) { return SDL_PushEvent(&e) == 1; };
        const auto key = [&](SDL_Keycode sym) {
            SDL_Event e;
            std::memset(&e, 0, sizeof(e));
            e.type = SDL_KEYDOWN;
            e.key.keysym.sym = sym;
            return push(e);
        };
        const auto text = [&](const char* s) {
            SDL_Event e;
            std::memset(&e, 0, sizeof(e));
            e.type = SDL_TEXTINPUT;
            std::snprintf(e.text.text, sizeof(e.text.text), "%s", s);
            return push(e);
        };
        const auto click = [&](int32_t x, int32_t y) {
            SDL_Event motion;
            std::memset(&motion, 0, sizeof(motion));
            motion.type = SDL_MOUSEMOTION;
            motion.motion.x = x;
            motion.motion.y = y;
            SDL_Event press = motion;
            press.type = SDL_MOUSEBUTTONDOWN;
            press.button.x = x;
            press.button.y = y;
            press.button.button = SDL_BUTTON_LEFT;
            press.button.clicks = 1;
            SDL_Event release = press;
            release.type = SDL_MOUSEBUTTONUP;
            return push(motion) && push(press) && push(release);
        };
        const LayoutRect lines_box = chat.lines;
        const LayoutRect input = chat.input_box;
        const LayoutRect outside{0, 0, chat.box.x - 4, 540};                                       // (the left column of the screen: not the chat's)
        const auto park = [&]() {                                                                  // (the pointer is drawn by the game: it is put on the clay, away from what is measured)
            SDL_Event motion;
            std::memset(&motion, 0, sizeof(motion));
            motion.type = SDL_MOUSEMOTION;
            motion.motion.x = 700;
            motion.motion.y = 20;
            return push(motion);
        };
        ASSERT_TRUE(park());
        app.map_select().update(0.6f);                                                             // (the screen's labels, and the chat's lines, are drawn from 500 ms after it was created)
        app.run_frame_with_delta(0.016f);
        const uint64_t lines_empty = region_hash(app, lines_box);
        const uint64_t input_empty = region_hash(app, input);
        const uint64_t outside_before = region_hash(app, outside);
        ASSERT_TRUE(lines_empty != 0 && input_empty != 0);
        // a line of the room changes the lines box and nothing else of the column
        ASSERT_TRUE(d.peer.net.chat("a line for the box"));
        ASSERT_TRUE(d.hall.until([&]() { return app.map_select().chat_panel().lines.size() == 1; }, 8000));
        app.run_frame_with_delta(0.016f);
        ASSERT_TRUE(region_hash(app, lines_box) != lines_empty);
        ASSERT_EQ(region_hash(app, input), input_empty);
        // the click on the input box opens it (the picture is the canvas: window coordinates are the picture's)
        ASSERT_TRUE(click(centre_x(input), centre_y(input)));
        app.run_frame_with_delta(0.016f);
        ASSERT_TRUE(app.room_chat().is_open());
        ASSERT_TRUE(park());
        ASSERT_TRUE(text("hi there"));
        app.run_frame_with_delta(0.016f);
        ASSERT_EQ(app.room_chat().text(), std::string("hi there"));
        ASSERT_EQ(app.map_select().chat_panel().typed, std::string("hi there"));
        // the typed text is in the input box's picture, and the caret blinks in it: both states differ from each other and from the empty box
        std::set<uint64_t> seen;
        for (int i = 0; i < 40; ++i) {
            d.hall.step(20);
            app.run_frame_with_delta(0.001f);
            seen.insert(region_hash(app, input));
        }
        ASSERT_EQ(seen.size(), size_t{2});
        ASSERT_TRUE(seen.count(input_empty) == 0);
        // an S while the line is typed is not START, Enter sends it, and the box that showed the typed text is empty again; the other lines of the screen did not move
        ASSERT_TRUE(key(SDLK_s));
        app.run_frame_with_delta(0.016f);
        d.hall.step(500);
        ASSERT_TRUE(app.room_chat().is_open() && !app.map_select().is_locked() && d.server.status("BOX-EVENTS").state == server::RoomState::Waiting);
        ASSERT_TRUE(key(SDLK_RETURN));
        app.run_frame_with_delta(0.016f);
        ASSERT_FALSE(app.room_chat().is_open());
        ASSERT_TRUE(d.hall.until([&]() { return d.peer.net.pregame_chat().size() == 2; }, 8000));
        ASSERT_EQ(lines(d.peer.net.pregame_chat()), (std::vector<std::string>{"1|Bob|a line for the box", "0|Ana|hi there"}));
        ASSERT_TRUE(d.hall.until([&]() { return app.map_select().chat_panel().lines.size() == 2; }, 8000));
        app.run_frame_with_delta(0.016f);
        ASSERT_EQ(region_hash(app, input), input_empty);
        ASSERT_EQ(region_hash(app, outside), outside_before);
        // T from the keyboard opens it the same way
        d.hall.step(500);
        ASSERT_TRUE(key(SDLK_t));
        ASSERT_TRUE(text("t"));
        app.run_frame_with_delta(0.016f);
        ASSERT_TRUE(app.room_chat().is_open() && app.room_chat().text().empty());
        app.quit();
    } TEST_END();

    // ---- the meeting of the online rooms and the mouse-wheel zoom (v0.1.0: the two were written apart, on v0.0.99): N5.68 - N5.72 ----
    // The wheel, the middle button and the level of the zoom through the application's own entry points, and through the window's event queue where the screen's own routing is part of the
    // question (a setup screen takes its events in handle_events, not in handle_mouse_wheel)
    uint32_t wheel_clock = 500000;
    const auto wheel_event = [&wheel_clock](int32_t y, float precise) {
        SDL_MouseWheelEvent e{};
        e.type = SDL_MOUSEWHEEL;
        wheel_clock += 16;
        e.timestamp = wheel_clock;
        e.y = y;
        e.preciseY = precise;
        e.direction = SDL_MOUSEWHEEL_NORMAL;
        return e;
    };
    const auto notch = [&](Application& app, int notches) {
        for (int i = 0; i < std::abs(notches); ++i) app.handle_mouse_wheel(wheel_event(notches > 0 ? 1 : -1, notches > 0 ? 1.0f : -1.0f));
    };
    const auto middle_click = [](Application& app, int32_t x, int32_t y) {
        SDL_MouseButtonEvent b{};
        b.type = SDL_MOUSEBUTTONDOWN;
        b.button = SDL_BUTTON_MIDDLE;
        b.state = SDL_PRESSED;
        b.clicks = 1;
        b.x = x;
        b.y = y;
        app.handle_mouse_button(b);
        b.type = SDL_MOUSEBUTTONUP;
        b.state = SDL_RELEASED;
        app.handle_mouse_button(b);
    };
    const auto queue = [](SDL_Event e) { return SDL_PushEvent(&e) == 1; };
    const auto queue_motion = [&](int32_t x, int32_t y) {
        SDL_Event e;
        std::memset(&e, 0, sizeof(e));
        e.type = SDL_MOUSEMOTION;
        e.motion.x = x;
        e.motion.y = y;
        return queue(e);
    };
    const auto queue_wheel = [&](int32_t y, float precise) {
        SDL_Event e;
        std::memset(&e, 0, sizeof(e));
        e.type = SDL_MOUSEWHEEL;
        e.wheel.y = y;
        e.wheel.preciseY = precise;
        e.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
        return queue(e);
    };
    const auto queue_button = [&](uint8_t button, bool down, int32_t x, int32_t y) {
        SDL_Event e;
        std::memset(&e, 0, sizeof(e));
        e.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
        e.button.button = button;
        e.button.state = down ? SDL_PRESSED : SDL_RELEASED;
        e.button.clicks = 1;
        e.button.x = x;
        e.button.y = y;
        return queue(e);
    };
    const auto queue_text = [&](const char* text) {
        SDL_Event e;
        std::memset(&e, 0, sizeof(e));
        e.type = SDL_TEXTINPUT;
        std::snprintf(e.text.text, sizeof(e.text.text), "%s", text);
        return queue(e);
    };
    const auto queue_key = [&](SDL_Keycode sym) {
        SDL_Event e;
        std::memset(&e, 0, sizeof(e));
        e.type = SDL_KEYDOWN;
        e.key.keysym.sym = sym;
        return queue(e);
    };
    // the picture of a rectangle of the canvas: the number of distinct colours in it (a picture of a map has many, the "No preview" box a handful)
    const auto region_colours = [](Application& app, const LayoutRect& r) {
        SDL_Rect rect{r.x, r.y, r.w, r.h};
        std::vector<uint8_t> px(static_cast<size_t>(r.w) * static_cast<size_t>(r.h) * 4u, 0);
        if (SDL_RenderReadPixels(app.renderer().get_sdl_renderer(), &rect, SDL_PIXELFORMAT_RGBA32, px.data(), r.w * 4) != 0) return 0;
        std::set<uint32_t> seen;
        for (size_t i = 0; i + 3 < px.size(); i += 4) seen.insert(static_cast<uint32_t>(px[i]) << 16 | static_cast<uint32_t>(px[i + 1]) << 8 | px[i + 2]);
        return static_cast<int>(seen.size());
    };
    // START of the room's leader (Enter, the chat input closed): the match begins on this machine and on Bob's
    const auto start_room_match = [&](BoxRoom& d, const std::string& code) -> bool {
        d.app.room_key_down(SDLK_RETURN, 0, false);
        if (!d.hall.until([&]() { return d.app.state() == AppState::Playing && d.peer.net.phase() == net::NetGame::Phase::Playing; }, 15000)) return false;
        const server::RoomStatus st = d.server.status(code);
        d.app.hud().dismiss_match_start_modal();
        return st.state == server::RoomState::Running && st.bots.size() == 2;                // the fill seated the two empty seats
    };

    TEST_CASE("N5.68 The Wheel And The Middle Button Do Nothing On The 16:9 Setup Screens Of A Room (Leader And Guest, With Their Chat Box) - Not Over The Chat Box, Its Input Box, The Map Preview Or Anywhere Else - Whether The Chat Input Is Closed Or Open: Nothing Zooms, Nothing Opens Or Closes The Input, What Is Typed Stays, The Pictures Stay") {
        for (const bool leader : {true, false}) {
            BoxRoom d;
            const std::string code = leader ? "WZ-LEAD" : "WZ-GUEST";
            ASSERT_TRUE(leader ? open_leader(d, code, Aspect::Wide16x9, net::FillLevel::Medium, false, true, 2.0f, true) : open_guest(d, code, Aspect::Wide16x9, net::FillLevel::None, 2.0f, true));
            Application& app = d.app;
            const SetupVariant variant = leader ? SetupVariant::Online : SetupVariant::Guest;
            const SetupLayout& layout = SL::of(variant);
            ASSERT_TRUE(app.map_select().wide_layout() && app.map_select().setup_variant() == variant && app.map_select().chat_panel().visible);
            ASSERT_TRUE(app.remembered_zoom() == 2.0f && app.zoom() == 2.0f);                  // (the setup screen's camera is at the remembered level until a match starts; a wheel or a middle button that leaked would change it)
            app.map_select().update(0.6f);
            const LayoutRect preview = layout.preview_area();
            struct Spot {
                const char* what;
                int32_t x;
                int32_t y;
            };
            const std::vector<Spot> spots = {
                {"the chat box's lines", centre_x(layout.chat.lines), centre_y(layout.chat.lines)},
                {"the chat box's input box", centre_x(layout.chat.input_box), centre_y(layout.chat.input_box)},
                {"the map preview", centre_x(preview), centre_y(preview)},
                {"the clay at the top right", 700, 20},
                {"where a match's map view would be", 300, 200},
            };
            const LayoutRect outside{0, 0, layout.chat.box.x - 4, 540};                          // (the screen left of the chat column, the preview in it)
            struct Snap {
                float zoom{0};
                float remembered{0};
                float camera_zoom{0};
                bool open{false};
                std::string typed;
                std::string text;
                size_t lines{0};
                bool locked{false};
                AppState state{AppState::Loading};
                net::NetGame::Phase phase{net::NetGame::Phase::Off};
                uint64_t lines_px{0};
                uint64_t outside_px{0};
            };
            const auto park = [&]() { return queue_motion(700, 20); };
            const auto snap = [&]() {
                park();
                app.run_frame_with_delta(0.016f);
                Snap s;
                s.zoom = app.zoom();
                s.remembered = app.remembered_zoom();
                s.camera_zoom = app.renderer().camera().zoom;
                s.open = app.room_chat().is_open();
                s.typed = app.map_select().chat_panel().typed;
                s.text = app.room_chat().text();
                s.lines = app.map_select().chat_panel().lines.size();
                s.locked = app.map_select().is_locked();
                s.state = app.state();
                s.phase = app.net()->phase();
                s.lines_px = region_hash(app, layout.chat.lines);
                s.outside_px = region_hash(app, outside);
                return s;
            };
            const auto same = [](const Snap& a, const Snap& b) {
                return a.zoom == b.zoom && a.remembered == b.remembered && a.camera_zoom == b.camera_zoom && a.open == b.open && a.typed == b.typed && a.text == b.text && a.lines == b.lines &&
                       a.locked == b.locked && a.state == b.state && a.phase == b.phase && a.lines_px == b.lines_px && a.outside_px == b.outside_px;
            };
            // the wheel and the middle button at every spot, by the application's entry points and through the event queue
            const auto roll_everywhere = [&](const char* when) -> bool {
                for (const Spot& spot : spots) {
                    const Snap before = snap();
                    if (!queue_motion(spot.x, spot.y)) return false;
                    app.run_frame_with_delta(0.016f);
                    if (app.view_zoom_allowed(spot.x, spot.y)) {
                        std::cout << "\n    [" << when << ", " << spot.what << "] the wheel would be allowed\n";
                        return false;
                    }
                    notch(app, +1);
                    notch(app, -1);
                    notch(app, -1);
                    app.handle_mouse_wheel(wheel_event(0, 0.6f));
                    app.handle_mouse_wheel(wheel_event(0, 0.6f));
                    middle_click(app, spot.x, spot.y);
                    if (!(queue_wheel(1, 1.0f) && queue_wheel(-1, -1.0f) && queue_wheel(0, 0.6f) && queue_wheel(0, 0.6f) && queue_button(SDL_BUTTON_MIDDLE, true, spot.x, spot.y) &&
                          queue_button(SDL_BUTTON_MIDDLE, false, spot.x, spot.y))) {
                        return false;
                    }
                    app.run_frame_with_delta(0.016f);
                    d.hall.step(30);
                    const Snap after = snap();
                    if (!same(before, after)) {
                        std::cout << "\n    [" << when << ", " << spot.what << "] something changed: zoom " << before.zoom << " -> " << after.zoom << ", remembered " << before.remembered << " -> " << after.remembered
                                  << ", input open " << before.open << " -> " << after.open << ", typed '" << before.typed << "' -> '" << after.typed << "', lines " << before.lines << " -> " << after.lines
                                  << ", pictures " << (before.lines_px == after.lines_px && before.outside_px == after.outside_px ? "same" : "differ") << "\n";
                        return false;
                    }
                }
                return true;
            };
            // 1. the chat input closed: nothing opens it either (a middle click over the input box is not a click)
            ASSERT_TRUE(roll_everywhere("input closed"));
            ASSERT_FALSE(app.room_chat().is_open());
            // 2. the chat input open, with a line typed
            app.room_key_down(SDLK_t, 0, false);
            app.room_text_input("t");
            app.room_text_input("abc");
            d.hall.step(30);
            ASSERT_TRUE(app.room_chat().is_open() && app.room_chat().text() == "abc" && app.map_select().chat_panel().typed == "abc");
            ASSERT_TRUE(roll_everywhere("input open"));
            ASSERT_TRUE(app.room_chat().is_open() && app.room_chat().text() == "abc" && app.map_select().chat_panel().typed == "abc");
            // 3. the line is sent as it was typed, and the room is still the room
            app.room_key_down(SDLK_RETURN, 0, false);
            ASSERT_TRUE(d.hall.until([&]() { return !d.peer.net.pregame_chat().empty(); }, 8000));
            ASSERT_EQ(d.peer.net.pregame_chat().back().text, std::string("abc"));
            ASSERT_TRUE(!app.room_chat().is_open() && app.state() == AppState::MapSelect && app.net()->phase() == net::NetGame::Phase::Room && !app.map_select().is_locked());
            ASSERT_TRUE(app.remembered_zoom() == 2.0f && app.zoom() == 2.0f);
            app.quit();
        }
    } TEST_END();

    // The zoom levels are the player's own view: a match of the network offers the levels of a local game on the same map and screen (the series 2^(k/4) from 2 down to the map's limit,
    // max(view_w / map_w, view_h / map_h): TINY (992 x 992) over the 16:9 view of 762 x 500 ends at 762 / 992 = 0.768), and the wheel zooms through them in the match. Nothing of it is sent.
    const auto tiny_wide_levels_ok = [](const std::vector<float>& levels) {
        if (levels.size() != 7) return false;
        const float want[6] = {2.0f, 1.6817928f, 1.4142135f, 1.1892071f, 1.0f, 0.8408964f};
        for (int i = 0; i < 6; ++i) {
            if (std::fabs(levels[static_cast<size_t>(i)] - want[i]) > 1e-6f) return false;
        }
        return levels[6] >= 762.0f / 992.0f && levels[6] < 762.0f / 992.0f + 1e-5f;                  // the limit is the last level
    };
    const auto nearest_to = [](float z, const std::vector<float>& list) {
        float best = list.front();
        for (const float l : list) {
            if (std::fabs(std::log(z / l)) < std::fabs(std::log(z / best))) best = l;
        }
        return best;
    };

    TEST_CASE("N5.69 A Match Started From A Room With Bots (The Leader's Fill, A Server's Room) Has The Zoom Levels Of A Local Game On The Same Map And Screen: It Starts At The Nearest One To The Remembered 0.5 (Which Stays Remembered), The Wheel Zooms Out And In Through Them In The Match, And The Local Game After It Starts At The Level That Was Chosen") {
        BoxRoom d;
        ASSERT_TRUE(open_leader(d, "ZM-FILL", Aspect::Wide16x9, net::FillLevel::Medium, false, true, 0.5f, true));
        Application& app = d.app;
        ASSERT_TRUE(app.remembered_zoom() == 0.5f && app.zoom() == 0.5f);                    // (the setup screen's camera is at the remembered level until a match starts)
        ASSERT_TRUE(start_room_match(d, "ZM-FILL"));
        ASSERT_TRUE(app.network_active());
        const LayoutRect view = app.layout().view();
        const std::vector<float> levels = app.zoom_levels();
        ASSERT_TRUE(app.zoom_limits() == zoom::Limits::any() && tiny_wide_levels_ok(levels));      // (no limit of the kind of match)
        ASSERT_TRUE(app.zoom() == levels.back() && app.remembered_zoom() == 0.5f);           // 0.5 is below this map's limit: the match starts at the limit, 0.5 stays remembered
        app.note_pointer(view.x + 300, view.y + 200);
        notch(app, -1);
        ASSERT_EQ(app.zoom(), levels.back());                                                // the wheel toward cannot go beyond the map's limit
        app.handle_mouse_wheel(wheel_event(0, -0.6f));
        app.handle_mouse_wheel(wheel_event(0, -0.6f));
        ASSERT_EQ(app.zoom(), levels.back());                                                // nor can a trackpad's creep
        ASSERT_TRUE(!app.set_zoom(0.5f, view.x + 300, view.y + 200) && !app.step_zoom(-1, view.x + 300, view.y + 200));        // (0.5 is no level here: the nearest is where the view is)
        ASSERT_EQ(app.remembered_zoom(), 0.5f);                                              // none of that changed what is remembered
        notch(app, +1);                                                                      // one notch in: the next level
        ASSERT_EQ(app.zoom(), levels[5]);
        ASSERT_EQ(app.remembered_zoom(), levels[5]);                                         // a level that is chosen in the match is remembered
        app.handle_mouse_wheel(wheel_event(0, 0.6f));
        ASSERT_EQ(app.zoom(), levels[5]);                                                    // a trackpad's creep adds up: 0.6 is no notch ...
        app.handle_mouse_wheel(wheel_event(0, 0.6f));
        ASSERT_EQ(app.zoom(), levels[4]);                                                    // ... and 1.2 is one
        notch(app, +2);
        ASSERT_EQ(app.zoom(), levels[2]);
        middle_click(app, view.x + 300, view.y + 200);
        ASSERT_EQ(app.zoom(), 1.0f);                                                         // the middle button: back to 1
        notch(app, -2);
        ASSERT_EQ(app.zoom(), levels.back());                                                // two notches out from 1: the limit again
        ASSERT_TRUE(queue_motion(view.x + 300, view.y + 200) && queue_wheel(1, 1.0f));
        app.run_frame_with_delta(0.016f);                                                    // (through the window's own event loop, in the match)
        ASSERT_EQ(app.zoom(), levels[5]);
        ASSERT_TRUE(queue_motion(view.x + 300, view.y + 200) && queue_button(SDL_BUTTON_MIDDLE, true, view.x + 300, view.y + 200) && queue_button(SDL_BUTTON_MIDDLE, false, view.x + 300, view.y + 200));
        app.run_frame_with_delta(0.016f);
        ASSERT_EQ(app.zoom(), 1.0f);
        ASSERT_EQ(app.remembered_zoom(), 1.0f);
        d.hall.step(kDialogMs + 3000);                                                       // the match goes on at a zoom of the player's own, with the server's bots (its first turn is sealed kDialogMs after it began)
        ASSERT_TRUE(!app.net()->desynced() && d.server.status("ZM-FILL").ticks > 40);
        app.note_pointer(view.x + 300, view.y + 200);                                        // (a queued button release in a headless run reads as a release outside the window)
        notch(app, -1);                                                                      // the level that the match ends at: what the next game starts with
        ASSERT_EQ(app.zoom(), levels[5]);
        app.return_to_map_select();
        ASSERT_TRUE(!app.network_active() && app.zoom_limits() == zoom::Limits::any());
        ASSERT_TRUE(app.start_game(maps_dir() + "TINY.LVL"));
        app.hud().dismiss_match_start_modal();
        ASSERT_TRUE(app.zoom() == levels[5] && app.remembered_zoom() == levels[5]);
        ASSERT_TRUE(app.zoom_levels() == levels);                                            // (the same levels as the match of the network had)
        app.note_pointer(view.x + 300, view.y + 200);
        notch(app, -1);
        ASSERT_EQ(app.zoom(), levels.back());
        app.quit();
    } TEST_END();

    TEST_CASE("N5.70 Typing In The Match's Chat While Zooming (A Match From A Room With Bots): The Typed Line Is The Same Through Every Zoom Step Of The Wheel And The Middle Button - The Keys Of The Line (+ - =) Do Not Zoom - Enter Sends It Whole To The Room, And The Level Chosen In The Match Is What The Next Local Game Starts With") {
        BoxRoom d;
        ASSERT_TRUE(open_leader(d, "ZM-CHAT", Aspect::Wide16x9, net::FillLevel::Medium, false, true));
        Application& app = d.app;
        ASSERT_TRUE(start_room_match(d, "ZM-CHAT"));
        const LayoutRect view = app.layout().view();
        const int32_t px = view.x + 300;
        const int32_t py = view.y + 200;
        const float in1 = zoom::series(1);                                                   // one level in from 1 (1.19) and one out (0.84): the levels are not 2 and 0.5 any more
        const float out1 = zoom::series(-1);
        ASSERT_TRUE(queue_motion(px, py));
        app.run_frame_with_delta(0.016f);
        ASSERT_TRUE(app.zoom() == 1.0f && app.hud().get_chat_input().empty());
        ASSERT_TRUE(queue_text("hel"));
        app.run_frame_with_delta(0.016f);
        ASSERT_EQ(app.hud().get_chat_input(), std::string("hel"));
        ASSERT_TRUE(queue_wheel(1, 1.0f));                                                   // zoom in, in the middle of the line
        app.run_frame_with_delta(0.016f);
        ASSERT_TRUE(app.zoom() == in1 && app.hud().get_chat_input() == "hel");
        ASSERT_TRUE(queue_text("lo"));
        app.run_frame_with_delta(0.016f);
        ASSERT_EQ(app.hud().get_chat_input(), std::string("hello"));
        ASSERT_TRUE(queue_text(" +-="));                                                     // the characters that a zoom key would have: they are text, the zoom stays
        app.run_frame_with_delta(0.016f);
        ASSERT_TRUE(app.zoom() == in1 && app.hud().get_chat_input() == "hello +-=");
        ASSERT_TRUE(queue_button(SDL_BUTTON_MIDDLE, true, px, py) && queue_button(SDL_BUTTON_MIDDLE, false, px, py));       // the middle button: back to 1, the line stays
        app.run_frame_with_delta(0.016f);
        ASSERT_TRUE(app.zoom() == 1.0f && app.hud().get_chat_input() == "hello +-=");
        ASSERT_TRUE(queue_motion(px, py) && queue_wheel(-1, -1.0f));                         // toward: zoomed out one level (a queued button release in a headless run reads as a release outside the window: the motion puts the pointer back)
        app.run_frame_with_delta(0.016f);
        ASSERT_TRUE(app.zoom() == out1 && app.hud().get_chat_input() == "hello +-=");
        ASSERT_TRUE(queue_text(" team"));
        app.run_frame_with_delta(0.016f);
        ASSERT_EQ(app.hud().get_chat_input(), std::string("hello +-= team"));
        ASSERT_TRUE(queue_key(SDLK_RETURN));                                                 // Enter: the line goes to the room whole
        app.run_frame_with_delta(0.016f);
        ASSERT_TRUE(app.hud().get_chat_input().empty() && log_has(app.hud().get_chat_log(), "hello +-= team"));
        ASSERT_TRUE(d.hall.until([&]() { return !d.peer.chats.empty(); }, 8000));
        ASSERT_EQ(d.peer.chats.back().text, std::string("hello +-= team"));
        ASSERT_TRUE(queue_motion(px, py) && queue_wheel(2, 2.0f));                           // zoomed in two levels (0.84 -> 1 -> 1.19), a second line while zoomed, and the match goes on
        app.run_frame_with_delta(0.016f);
        ASSERT_EQ(app.zoom(), in1);
        ASSERT_TRUE(queue_text("second"));
        app.run_frame_with_delta(0.016f);
        ASSERT_TRUE(queue_key(SDLK_RETURN));
        app.run_frame_with_delta(0.016f);
        ASSERT_TRUE(d.hall.until([&]() { return d.peer.chats.size() == 2; }, 8000));
        ASSERT_EQ(d.peer.chats.back().text, std::string("second"));
        d.hall.step(2000);
        ASSERT_TRUE(!app.net()->desynced() && app.zoom() == in1 && app.remembered_zoom() == in1);
        app.return_to_map_select();                                                          // the level chosen in the match is what a local game starts with
        ASSERT_TRUE(app.start_game(maps_dir() + "TINY.LVL"));
        ASSERT_EQ(app.zoom(), in1);
        app.quit();
    } TEST_END();

    TEST_CASE("N5.71 The Same For The Host Of A Room On The Local Network Whose START Seats Bots Itself (This Machine Runs Them): The Match Is A Match Of The Network With The Zoom Levels Of A Local Game On Its Map - It Starts At The Nearest One To The Remembered 0.5, The Wheel Zooms Out And In, Bots Or Not - And The Local Game After It Has The Same Levels") {
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Host;
        cfg.net_port = 0;
        cfg.net_loopback_only = true;
        cfg.player_name = "Alice";
        cfg.fill_bots = net::FillLevel::Easy;
        cfg.aspect = Aspect::Wide16x9;
        cfg.aspect_given = true;
        cfg.has_window_size = true;
        cfg.window_w = 960;
        cfg.window_h = 540;
        cfg.zoom = 0.5f;
        cfg.zoom_given = true;
        Application app;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_TRUE(app.remembered_zoom() == 0.5f && app.zoom() == 0.5f);
        Peer bob;
        ASSERT_TRUE(bob.net.join("127.0.0.1", app.net()->listen_port(), "Bob"));
        Duo duo{app, bob};
        ASSERT_TRUE(duo.until([&]() { return app.net()->room().slots[1].state == net::SlotState::Client && app.net()->room().slots[1].rtt_ms != net::kRttUnknown; }, 8000));
        app.map_select().handle_key_down(SDLK_RETURN);
        ASSERT_TRUE(duo.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 8000));
        ASSERT_TRUE(app.bots() != nullptr && app.bots()->seat_mask() == 0x0C);               // this machine runs the two bots of the fill ...
        ASSERT_TRUE(app.network_active());                                                   // ... and it is still a match of the network
        app.hud().dismiss_match_start_modal();
        const LayoutRect view = app.layout().view();
        const std::vector<float> levels = app.zoom_levels();
        ASSERT_TRUE(app.zoom_limits() == zoom::Limits::any() && levels.size() >= 5 && levels.front() == 2.0f);      // (no limit of the kind of match)
        ASSERT_TRUE(std::count(levels.begin(), levels.end(), 1.0f) == 1 && std::is_sorted(levels.rbegin(), levels.rend()));
        const float start = nearest_to(0.5f, levels);
        ASSERT_TRUE(app.zoom() == start && app.remembered_zoom() == 0.5f);
        const int32_t map_px = static_cast<int32_t>(app.sim().grid().width()) * 32;          // the last level is the map's limit: the view's smaller-relative side just fills it
        const float limit = std::max(static_cast<float>(view.w) / static_cast<float>(map_px), static_cast<float>(view.h) / static_cast<float>(static_cast<int32_t>(app.sim().grid().height()) * 32));
        ASSERT_TRUE(levels.back() >= limit && levels.back() < limit + 1e-5f);
        app.note_pointer(view.x + 300, view.y + 200);
        notch(app, +1);
        ASSERT_TRUE(app.zoom() > start && app.remembered_zoom() == app.zoom());              // zooming in works, and it is what is remembered then
        notch(app, -3);
        ASSERT_TRUE(app.zoom() < start || app.zoom() == levels.back());                      // zooming out works (or the view is at the map's limit)
        const float chosen = app.zoom();
        duo.step(3000 + kDialogMs);                                                          // (the host seals the first turn kDialogMs after the match began; the bots look from the first tick on)
        ASSERT_TRUE(!app.net()->desynced() && !bob.net.desynced() && app.bots()->stats(2).decisions > 0);
        ASSERT_TRUE(app.zoom() == chosen);
        app.return_to_map_select();
        ASSERT_TRUE(!app.network_active() && app.bots() == nullptr && app.zoom_limits() == zoom::Limits::any());
        ASSERT_TRUE(app.start_game(maps_dir() + "GAUNTLET.LVL"));                            // a local game on a map of 1920 x 1920 (this level list is a different one: the match's map may be another)
        ASSERT_TRUE(app.zoom() == nearest_to(chosen, app.zoom_levels()) && app.zoom_levels().front() == 2.0f);
        app.quit();
    } TEST_END();

    TEST_CASE("N5.72 The Map Preview Of The 16:9 Setup Screens Of A Room (Leader And Guest) Is The Zoom 1 Picture Whatever The Camera Is At (--zoom 0.5 Or 2: The Camera Of The Setup Screen Is At The Remembered Level, As A Zoomed Match Leaves It): The Same Pixels As With The Camera At 1, A Picture Of The Map, And The Live Camera Keeps Its Zoom") {
        struct Shot {
            bool ok{false};
            uint64_t hash{0};
            int colours{0};
            float camera_zoom{0};
        };
        const auto preview_after = [&](bool leader, float level, int index) {
            Shot out;
            BoxRoom d;
            const std::string code = "PV-" + std::string(leader ? "L" : "G") + std::to_string(index);
            // (--zoom LEVEL --join ...: the camera of the setup screen is at the remembered level from the start, as a zoomed match leaves it; no preview has been made yet: no frame has run)
            const bool opened = leader ? open_leader(d, code, Aspect::Wide16x9, net::FillLevel::None, false, true, level, true) : open_guest(d, code, Aspect::Wide16x9, net::FillLevel::None, level, true);
            if (!opened) return out;
            Application& app = d.app;
            const LayoutRect area = SL::of(leader ? SetupVariant::Online : SetupVariant::Guest).preview_area();
            app.map_select().update(0.6f);
            app.run_frame_with_delta(0.016f);
            out.hash = region_hash(app, area);
            out.colours = region_colours(app, area);
            out.camera_zoom = app.renderer().camera().zoom;
            out.ok = out.hash != 0;
            app.quit();
            return out;
        };
        int index = 0;
        for (const bool leader : {true, false}) {
            const Shot at1 = preview_after(leader, 1.0f, index++);
            ASSERT_TRUE(at1.ok && at1.colours > 30 && at1.camera_zoom == 1.0f);              // (a picture of the map, not the "No preview" box)
            for (const float level : {0.5f, 2.0f}) {
                const Shot other = preview_after(leader, level, index++);
                if (!(other.ok && other.hash == at1.hash && other.colours == at1.colours && other.camera_zoom == level)) {
                    std::cout << "\n    [" << (leader ? "leader" : "guest") << ", camera at " << level << "] hash " << other.hash << " vs " << at1.hash << ", colours " << other.colours << " vs " << at1.colours
                              << ", camera " << other.camera_zoom << "\n";
                }
                ASSERT_TRUE(other.ok && other.hash == at1.hash && other.colours == at1.colours && other.camera_zoom == level);
            }
        }
    } TEST_END();

    TEST_CASE("N5.73 A Double Click On START After Typing Does Not Start The Match (The Review Of v0.1.0): The First Click Closes The Chat Input, The Rest Of Its Click Sequence Is Not The Screen's - A Press That SDL Counts As The Second Of A Double Click, And One That Comes Within The Double-Click Time Of The Closing Click - A Fresh Single Click 600 ms Later Is START (The 16:9 Page And The Classic One)") {
        struct Page {
            Aspect aspect;
            const char* code;
        };
        for (const Page& page : {Page{Aspect::Wide16x9, "BOX-DBL-W"}, Page{Aspect::Classic4x3, "BOX-DBL-C"}}) {
            BoxRoom d;
            ASSERT_TRUE(open_leader(d, page.code, page.aspect, net::FillLevel::Medium, false, true));
            Application& app = d.app;
            const bool wide = page.aspect == Aspect::Wide16x9;
            const SetupLayout& layout = SL::of(SetupVariant::Online);
            const int32_t open_x = wide ? centre_x(layout.chat.input_box) : MapSelectScreen::LABEL_X + 20;       // (where a click opens the input: the chat box's input box, the classic page's status line)
            const int32_t open_y = wide ? centre_y(layout.chat.input_box) : MapSelectScreen::STATUS_Y + 10;
            const int32_t sx = wide ? centre_x(layout.start) : MapSelectScreen::BTN_START_X + 5;
            const int32_t sy = wide ? centre_y(layout.start) : MapSelectScreen::BTN_START_Y + 5;
            const auto push = [](Uint32 type, int32_t x, int32_t y, uint8_t clicks) {
                SDL_Event e;
                std::memset(&e, 0, sizeof(e));
                e.type = type;
                e.button.x = x;
                e.button.y = y;
                e.button.button = SDL_BUTTON_LEFT;
                e.button.clicks = clicks;
                e.button.state = type == SDL_MOUSEBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED;
                return SDL_PushEvent(&e) == 1;
            };
            const auto waiting = [&]() { return d.server.status(page.code).state == server::RoomState::Waiting && !app.map_select().is_locked() && app.state() == AppState::MapSelect; };
            const auto open_input = [&]() {
                app.room_mouse_down(open_x, open_y, SDL_BUTTON_LEFT);                          // (the input's own click, as a person's: press and release)
                app.room_mouse_up(open_x, open_y, SDL_BUTTON_LEFT);
                return app.room_chat().is_open();
            };
            app.map_select().update(0.6f);
            ASSERT_FALSE(app.closing_click_pending());

            // (a) typed, then a double click on START: SDL counts the presses 1 and 2; the first closes the input, the second is the rest of that click and starts nothing
            ASSERT_TRUE(open_input());
            app.room_text_input("on my way");
            ASSERT_TRUE(push(SDL_MOUSEBUTTONDOWN, sx, sy, 1));
            ASSERT_TRUE(push(SDL_MOUSEBUTTONUP, sx, sy, 1));
            ASSERT_TRUE(push(SDL_MOUSEBUTTONDOWN, sx, sy, 2));
            ASSERT_TRUE(push(SDL_MOUSEBUTTONUP, sx, sy, 2));
            app.run_frame_with_delta(0.016f);
            d.hall.step(2000);
            ASSERT_TRUE(!app.room_chat().is_open());
            ASSERT_TRUE(app.closing_click_pending());                                         // (the rule waits for a click that begins a new sequence)
            ASSERT_TRUE(!app.map_select().start_button().pressed());
            ASSERT_TRUE(waiting());

            // (b) a double click that SDL does not count (a hand moves the pointer between the clicks): the second press, 100 ms after the closing one, is the rest of it as well
            SDL_Delay(600);                                                                   // (the double-click time of (a) is over)
            ASSERT_TRUE(open_input());
            ASSERT_TRUE(push(SDL_MOUSEBUTTONDOWN, sx, sy, 1));
            ASSERT_TRUE(push(SDL_MOUSEBUTTONUP, sx, sy, 1));
            app.run_frame_with_delta(0.016f);
            ASSERT_TRUE(!app.room_chat().is_open() && app.closing_click_pending());
            SDL_Delay(100);
            ASSERT_TRUE(push(SDL_MOUSEBUTTONDOWN, sx, sy, 1));
            ASSERT_TRUE(push(SDL_MOUSEBUTTONUP, sx, sy, 1));
            app.run_frame_with_delta(0.016f);
            d.hall.step(1500);
            ASSERT_TRUE(app.closing_click_pending() && !app.map_select().start_button().pressed() && waiting());

            // (c) a click that begins a new sequence (600 ms after the closing click, SDL counts 1): the screen's own: START asks, and the server starts the match with the two of them and the fill
            SDL_Delay(600);
            ASSERT_TRUE(push(SDL_MOUSEBUTTONDOWN, sx, sy, 1));
            ASSERT_TRUE(push(SDL_MOUSEBUTTONUP, sx, sy, 1));
            app.run_frame_with_delta(0.016f);
            ASSERT_TRUE(!app.closing_click_pending());
            ASSERT_TRUE(d.hall.until([&]() { return app.state() == AppState::Playing && d.peer.net.phase() == net::NetGame::Phase::Playing; }, 15000));
            ASSERT_TRUE(app.map_select().is_locked());
            app.quit();
        }
    } TEST_END();
}

// The front page's one card (web/lobby.html: "New match") sends every player to the game page of a room of four seats with the card's plan, and the game starts the match by itself: `--seat` (the colour
// of the person: any of the four), `--fill-bots` (four words: a bot row is its level, You, Friend and Nobody are none) and `--start-when N` (1 + the Friend rows: the leader's game presses START once N
// people are in). The same plan and the same N are in every link of the room, because whoever connects first leads and only the leader's game starts the match.
void run_one_card_tests() {
    using L = net::FillLevel;
    server::ServerLimits limits;
    limits.demo_rooms = 4;
    limits.demo_map = "TINY.LVL";
    limits.demo_maps = {"TINY.LVL"};
    const auto join_config = [](const Server& server, const std::string& room, const std::string& name, uint8_t seat, const net::FillPlan& plan, uint8_t start_when) {
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Join;
        cfg.net_address = "127.0.0.1";
        cfg.net_port = server.port();
        cfg.net_room = room;
        cfg.player_name = name;
        cfg.net_seat = seat;
        cfg.fill_bots = plan;
        cfg.net_start_when = start_when;
        return cfg;
    };

    TEST_CASE("N5.83 One Card, The Game's Arguments: --seat, --fill-bots (Four Words: A Level For A Bot Row, none For You / Friend / Nobody) And --start-when N Together; The Seat Is Any Of The Four Colours And The Plan Keeps A Bot At Green") {
        std::vector<std::string> args = {"ants", "--join-url", "wss://play.example.org/ws", "--room", "demo-treasure-4p-t12-k7m2xq", "--seat", "2", "--fill-bots", "easy,medium,none,none", "--start-when", "1", "--name", "Ann", "--aspect", "16:9"};
        std::vector<char*> st;
        ApplicationConfig c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_TRUE(c.startup_error.empty());
        ASSERT_TRUE(c.net_url == "wss://play.example.org/ws" && c.net_room == "demo-treasure-4p-t12-k7m2xq" && c.net_seat == 2 && c.net_start_when == 1 && c.player_name == "Ann");
        ASSERT_TRUE(c.fill_bots == net::FillPlan(std::array<L, 4>{L::Easy, L::Medium, L::None, L::None}));        // (a bot at Green: the plan's seat 0 is a seat like the others)
        for (int seat = 0; seat < 4; ++seat) {                                                                   // every colour can be the person's
            const std::string text = std::to_string(seat);
            args = {"ants", "--join-url", "ws://localhost/ws", "--room", "demo-small-4p-abc", "--seat", text, "--start-when", "3"};
            c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
            ASSERT_TRUE(c.net_seat == seat && c.net_start_when == 3 && c.startup_error.empty());
        }
        for (int people = 1; people <= 4; ++people) {                                                            // You and up to three friends: one to four people to wait for
            const std::string text = std::to_string(people);
            args = {"ants", "--join-url", "ws://localhost/ws", "--room", "demo-small-4p-abc", "--start-when", text};
            c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
            ASSERT_TRUE(c.net_start_when == people && c.startup_error.empty());
        }
        for (const char* text : {"0", "5", "40", "-1", "x", ""}) {                                               // anything else is no hook (and no error: the page only ever passes 1 - 4)
            args = {"ants", "--join-url", "ws://localhost/ws", "--room", "demo-small-4p-abc", "--start-when", text};
            c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
            ASSERT_TRUE(c.net_start_when == 0 && c.startup_error.empty());
        }
    } TEST_END();

    TEST_CASE("N5.84 One Card, Bots Only: A Leader Who Sits At Any Colour With --start-when 1 Starts The Match At Once, The Bots Of The Plan In Their Seats (A Bot At Green Too), The Nobody Seat Empty, The Room's Teams Made; The Plan's Word For The Leader's Own Seat Is Ignored") {
        struct Row {
            uint8_t seat;
            std::array<L, 4> plan;
            const char* code;
            uint8_t mask;                          // the seats that play
            const char* teams;                     // "ffa" or "A+B"
            std::array<std::pair<uint8_t, const char*>, 2> bots;
        };
        const Row rows[] = {
            {2, {L::Easy, L::Medium, L::None, L::None}, "demo-tiny-4p-t12-abcdef", 0x07, "1+2", {{{0, "Bot (Easy)"}, {1, "Bot (Medium)"}}}},        // Blue, bots at Green and Red; Red + Blue are a team, Green plays alone
            {0, {L::None, L::None, L::Hard, L::Easy}, "demo-tiny-4p-abcdef", 0x0D, "ffa", {{{2, "Bot (Hard)"}, {3, "Bot (Easy)"}}}},                  // Green, bots at Blue and Black, Red is Nobody
            {3, {L::Medium, L::None, L::Hard, L::None}, "demo-tiny-4p-t02-abcdef", 0x0D, "0+2", {{{0, "Bot (Medium)"}, {2, "Bot (Hard)"}}}},          // Black; Green + Blue are a team (both bots), Black plays alone, Red is Nobody
            {2, {L::Easy, L::Medium, L::Hard, L::None}, "demo-tiny-4p-abcdef", 0x07, "ffa", {{{0, "Bot (Easy)"}, {1, "Bot (Medium)"}}}},                // a word for the leader's own seat (Hard at Blue): a person holds it, the word counts for nothing
        };
        for (const Row& row : rows) {
            Server server(limits);
            Application leader;
            ASSERT_TRUE(leader.init(join_config(server, row.code, "Ann", row.seat, net::FillPlan(row.plan), 1)));
            Hall hall{server, &leader, {}};
            ASSERT_TRUE(hall.until([&]() { return leader.state() == AppState::Playing; }, 20000));                // (the hook pressed START by itself: one person and the plan's bots)
            ASSERT_EQ(leader.net()->my_seat(), row.seat);                                                        // (the room of four took the colour that was asked for, whatever it is)
            server::RoomStatus s = server.status(row.code);
            ASSERT_TRUE(s.state == server::RoomState::Running && s.joined == 3 && s.bots.size() == 2);
            for (size_t i = 0; i < 2; ++i) ASSERT_TRUE(s.bots[i].seat == row.bots[i].first && s.bots[i].name == row.bots[i].second && s.bots[i].fill);
            ASSERT_EQ(s.names[row.seat], std::string("Ann"));
            ASSERT_EQ(leader.sim().roster_mask(), row.mask);                                                     // the person and the two bots play; the fourth seat stays empty
            ASSERT_EQ(s.teams, std::string(row.teams));
            ASSERT_EQ(server.status(row.code).ignored_start_requests, 0u);                                       // (one START, and it was the leader's)
            hall.step(kDialogMs + 1500);
            s = server.status(row.code);
            ASSERT_TRUE(s.state == server::RoomState::Running && s.ticks > 20 && !leader.net()->desynced());
            leader.quit();
        }
    } TEST_END();

    TEST_CASE("N5.85 One Card, A Friend Row: The Match Starts When The Friend Is In (--start-when 2), Whichever Of The Two Connected First And So Leads: The Same Roster, The Plan's Bot, The Seat Of The Nobody Row Empty, The Code's Teams; The Game That Does Not Lead Sends Nothing (No START Of Its Own Is Heard)") {
        for (const bool host_first : {true, false}) {
            Server server(limits);
            const std::string code = "demo-tiny-4p-t01-abcdef";                          // Green + Red are a team: Red is a bot of the plan, Green the host
            const net::FillPlan plan(std::array<L, 4>{L::None, L::Medium, L::None, L::None});          // Red a Medium bot, Blue is Nobody, Black is the friend's (none): the host is Green (none)
            const ApplicationConfig host_cfg = join_config(server, code, "Host", 0, plan, 2);
            const ApplicationConfig friend_cfg = join_config(server, code, "Pal", 3, plan, 2);          // the same plan and the same number for everybody
            Application first;
            Application second;
            ASSERT_TRUE(first.init(host_first ? host_cfg : friend_cfg));
            Hall hall{server, &first, {}};
            ASSERT_TRUE(hall.until([&]() { return first.net()->phase() == net::NetGame::Phase::Room && first.net()->is_leader(); }, 8000));
            hall.step(3000);                                                                            // alone in the room: the hook wants two people, nothing is pressed
            ASSERT_TRUE(server.status(code).state == server::RoomState::Waiting && server.status(code).joined == 1 && server.status(code).ignored_start_requests == 0);
            ASSERT_EQ(first.state(), AppState::MapSelect);
            ASSERT_TRUE(second.init(host_first ? friend_cfg : host_cfg));
            hall.second = &second;
            bool second_led = false;                                                                    // (the leader's flag is only there while the room waits: it is looked at at every step)
            ASSERT_TRUE(hall.until([&]() { second_led = second_led || second.net()->is_leader(); return first.state() == AppState::Playing && second.state() == AppState::Playing; }, 20000));
            ASSERT_FALSE(second_led);
            server::RoomStatus s = server.status(code);
            ASSERT_TRUE(s.state == server::RoomState::Running && s.joined == 3 && s.bots.size() == 1);
            ASSERT_TRUE(s.bots[0].seat == 1 && s.bots[0].name == "Bot (Medium)" && s.bots[0].fill);
            ASSERT_TRUE(s.names[0] == "Host" && s.names[3] == "Pal" && s.names[2].empty());              // (the Nobody seat stays empty: the room of four started with three)
            ASSERT_EQ(first.net()->my_seat(), host_first ? uint8_t{0} : uint8_t{3});
            ASSERT_EQ(second.net()->my_seat(), host_first ? uint8_t{3} : uint8_t{0});
            ASSERT_EQ(s.ignored_start_requests, 0u);                                                     // the game that does not lead pressed nothing (a START of a non-leader is ignored and counted)
            ASSERT_TRUE(first.sim().roster_mask() == 0x0B && second.sim().roster_mask() == 0x0B);
            ASSERT_EQ(s.teams, std::string("0+1"));                                                      // the code's team, made at this START
            ASSERT_TRUE(s.allies[0] == 1 && s.allies[1] == 0 && s.allies[3] == sim::ALLIANCE_NONE);
            hall.step(kDialogMs + 1500);
            ASSERT_TRUE(hall.identical(first.sim(), second.sim()));
            ASSERT_FALSE(first.net()->desynced() || second.net()->desynced());
            first.quit();
            second.quit();
        }
    } TEST_END();
}

}  // namespace

int main(int argc, char* argv[]) {
    // SDL's headers rename main to SDL_main (SDL2main on Windows calls it): the signature must be this one, or the linker finds no SDL_main (the build guard in CMakeLists.txt checks it)
    (void)argc;
    (void)argv;
    std::cout << "\n=======================================================\n [SUITE] Network port: the application (names, room, thumbs, start, match)\n"
                 "=======================================================\n";
    run_command_line_tests();
    run_bot_tests();
    run_window_tests();
    run_host_tests();
    run_guest_tests();
    run_leader_tests();
    run_latency_tests();
    run_hidden_page_tests();
    run_room_bot_tests();
    run_room_chat_ui_tests();
    run_room_chat_box_tests();
    run_one_card_tests();
    std::cout << "\n=======================================================\n Total Test Cases: " << g_test_count << "\n Total Assertions: " << g_assert_count
              << "\n Failed:           " << g_test_failures << "\n=======================================================\n";
    if (g_test_count == 0) {                                      // (a misspelt or forgotten filter must not turn the suite green)
        std::cout << "\n no test ran: the filter ANTS_TEST_FILTER matches no test of this suite\n";
        return 1;
    }
    return g_test_failures == 0 ? 0 : 1;
}
