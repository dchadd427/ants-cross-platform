// Tests of the network in the application: the command line (names, --host, --join), a headless Application as the host of a room and as a guest of
// one (the other side is a bare NetGame with its own simulation), the setup screen as the room with names and thumbs, the start, a match driven by the
// lock-step runner with commands and chat, a guest that leaves, a host that leaves. Real sockets on the loopback interface; one Application per test
// (SDL is initialised once per process).
#include "ants_ai/bot_view.hpp"
#include "ants_app/application.hpp"
#include "ants_app/lan_list.hpp"
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
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
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
using ants::sim::Command;
using ants::sim::CommandType;

#ifndef ORIGINAL_ASSETS_DIR
#define ORIGINAL_ASSETS_DIR "Original-Ants"
#endif

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
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
    server::RoomManager mgr{server::MapStore(std::string(ORIGINAL_ASSETS_DIR) + "/Maps")};
    std::unique_ptr<net::TcpListener> listener{net::TcpListener::listen(0, true)};
    uint32_t now{1000};

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
        // the ticks of the local loop reach the controller: the bots look at the world (the idle one never acts; the standard one is the worker bot of B3 until B4 and sends its ants to the food)
        for (int i = 0; i < 400; ++i) app.update_simulation(0.05f);
        ASSERT_EQ(app.sim().current_tick(), 400u);
        ASSERT_TRUE(app.bots()->stats(1).decisions >= 19 && app.bots()->stats(3).decisions >= 99);                      // medium: every 20 ticks, hard: every 4
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
        for (int i = 0; i < 100; ++i) app.update_simulation(0.05f);
        ASSERT_TRUE(app.bots()->stats(2).decisions >= 24);
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
        duo.step(15000);
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
        for (int i = 0; i < 200; ++i) app.update_simulation(0.05f);
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
        for (uint32_t t = 0; t < 30000; t += 10) {
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
        ASSERT_TRUE(app.net()->turns_executed() > 250);
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
        ASSERT_TRUE(app.net()->turns_executed() > before + 10);
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
        // the application's own team message goes to everybody through the host and is marked as a team message
        app.hud().set_chat_input("hi Bob");
        app.hud().send_chat(true);
        trio.step(800);
        bool bob_got = false;
        for (const auto& c : bob.chats) bob_got = bob_got || (c.text == "hi Bob" && c.team && c.sender == 2);
        ASSERT_TRUE(bob_got);
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
        ASSERT_TRUE(hall.until([&]() { return app.net()->room().slots[1].rtt_ms != net::kRttUnknown; }, 8000));
        ASSERT_TRUE(app.net()->is_leader() && app.map_select().leads_server_room());
        ASSERT_FALSE(bob.net.is_leader());
        ASSERT_FALSE(bob.net.request_start());                                              // a guest sends nothing
        ASSERT_EQ(bob.net.room().leader, 0);
        ASSERT_EQ(bob.net.status_text(), std::string(sim::strings::text(sim::strings::kWaitingForHost)));
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

    TEST_CASE("N5.22 Leader: --start-when N Is A Test Hook That Presses START For The Leader Of A Server's Room Once N Players Are In (2 To 4 Only); A Game That Does Not Say It Never Does") {
        {   // the command line
            std::vector<std::string> args = {"ants", "--join", "127.0.0.1:4001", "--room", "R-1", "--start-when", "2"};
            std::vector<char*> storage;
            ASSERT_EQ(Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, storage)).net_start_when, 2);
            for (const char* value : {"3", "4"}) {
                args = {"ants", "--start-when", value};
                ASSERT_EQ(Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, storage)).net_start_when, static_cast<uint8_t>(value[0] - '0'));
            }
            for (const char* value : {"0", "1", "5", "-2", "many", ""}) {                    // not a number of players of a match: no hook
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

    TEST_CASE("N5.26 LAN Host: The Original's Own Host Screen Is Not Guarded, Because The Original Is Not: The Second Click Of A Double Click On START! Presses The Host's START And Starts The Match, As A Held Enter Does") {
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

}  // namespace

int main() {
    std::cout << "\n=======================================================\n [SUITE] Network port: the application (names, room, thumbs, start, match)\n"
                 "=======================================================\n";
    run_command_line_tests();
    run_bot_tests();
    run_window_tests();
    run_host_tests();
    run_guest_tests();
    run_leader_tests();
    std::cout << "\n=======================================================\n Total Test Cases: " << g_test_count << "\n Total Assertions: " << g_assert_count
              << "\n Failed:           " << g_test_failures << "\n=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}
