// The fixtures that the parts of the network application test share (test_network_app.cpp holds main(); the cases are in the test_network_app_*.cpp files). Inline or in an
// anonymous namespace, so that every part is a self-contained translation unit.
#pragma once
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
#include "../common/ants_test_pause.hpp"
#include "../common/test_assert.hpp"
#include "ants_ai/bot_names.hpp"

using namespace ants;
using namespace ants::app;

// The name that the server's room with this code gives the bot of a seat (protocol 20): "Pip (bot, Hard)"; `level` is Easy, Medium or Hard
inline std::string bn(const std::string& code, unsigned seat, const std::string& level) {
    return ai::bot_label(ai::bot_seat_name(ai::bot_name_key(code), static_cast<uint8_t>(seat)), level);
}
using ants::sim::Command;
using ants::sim::CommandType;

inline int g_test_count = 0;
inline int g_test_failures = 0;
inline int g_assert_count = 0;

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


namespace {

inline std::string maps_dir() { return std::string(ORIGINAL_ASSETS_DIR) + "/Maps/"; }

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
                    sim.set_game_mode(s.game_mode());                         // (the mode of the Start, before the first init: what the application does)
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
            ants_test::short_pause();
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
            ants_test::short_pause();
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
            ants_test::short_pause();
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

inline ApplicationConfig headless_config() {
    ApplicationConfig cfg;
    cfg.headless = true;
    cfg.start_in_map_select = true;
    cfg.lan_port = 0;                                                      // the tests do not announce on the real network
    return cfg;
}

inline bool start_three(Peer& host, Peer& bob, Application& app, const std::string& app_name) {
    if (!host.net.host(0, "Alice", true)) return false;
    host.net.set_map("TINY.LVL");
    if (!bob.net.join("127.0.0.1", host.net.listen_port(), "Bob")) return false;
    for (int i = 0; i < 800 && bob.net.phase() != net::NetGame::Phase::Room; ++i) {          // Bob is seated first (seat 1), the application second
        host.now += 10;
        host.update();
        bob.now += 10;
        bob.update();
        ants_test::short_pause();
    }
    ants_test::real_time_tail([&]() { return bob.net.phase() == net::NetGame::Phase::Room; }, [&]() { host.update(); bob.update(); });
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

inline char** argv_of(std::vector<std::string>& args, std::vector<char*>& storage) {
    storage.clear();
    for (auto& a : args) storage.push_back(a.data());
    storage.push_back(nullptr);
    return storage.data();
}

inline bool log_has(const std::deque<std::string>& log, const std::string& needle) {
    for (const auto& line : log) {
        if (line.find(needle) != std::string::npos) return true;
    }
    return false;
}

// The chat box wraps a long text into several lines: the lines joined by one blank are the text again
inline std::string log_joined(const std::deque<std::string>& log) {
    std::string out;
    for (const auto& line : log) {
        if (!out.empty()) out += ' ';
        out += line;
    }
    return out;
}

inline Command order(uint8_t seat, uint32_t ant, int16_t x, int16_t y) {
    Command c;
    c.type = CommandType::GroupMove;
    c.issuer = seat;
    c.tile_x = x;
    c.tile_y = y;
    c.ants = {ant};
    return c;
}

// An open ground tile near a team's hill: a goal that the path finder accepts
inline bool open_goal_near_hill(const sim::SimulationEngine& sim, uint8_t team, int16_t& gx, int16_t& gy) {
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

inline std::vector<uint32_t> ants_of(const sim::SimulationEngine& s, uint8_t player) {
    std::vector<uint32_t> out;
    for (const auto& a : s.get_world_state().ants) {
        if (a.player_id == player) out.push_back(a.id);
    }
    return out;
}

}  // namespace

// The groups of cases, run by main() in test_network_app.cpp in this order
void run_command_line_tests();
void run_bot_tests();
void run_host_tests();
void run_guest_tests();
void run_leader_tests();
void run_window_tests();
void run_latency_tests();
void run_hidden_page_tests();
void run_room_bot_tests();
void run_room_chat_ui_tests();
void run_room_chat_box_tests();
void run_one_card_tests();
void run_rematch_app_tests();
