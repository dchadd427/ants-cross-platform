// Tests of the network in the application: the command line (names, --host, --join), a headless Application as the host of a room and as a guest of
// one (the other side is a bare NetGame with its own simulation), the setup screen as the room with names and thumbs, the start, a match driven by the
// lock-step runner with commands and chat, a guest that leaves, a host that leaves. Real sockets on the loopback interface; one Application per test
// (SDL is initialised once per process).
#include "ants_app/application.hpp"
#include "ants_net/netgame.hpp"
#include "ants_net/protocol.hpp"
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

    Peer() {
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
                net.report_loaded(ok);
            }
        }
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
        return cond();
    }
};

// A three-player match on SMALL.LVL: the host (Alice, seat 0) and Bob (seat 1) are bare machines, the application is seat 2 (named `app_name`)
bool start_three(Peer& host, Peer& bob, Application& app, const std::string& app_name);

ApplicationConfig headless_config() {
    ApplicationConfig cfg;
    cfg.headless = true;
    cfg.start_in_map_select = true;
    return cfg;
}

bool start_three(Peer& host, Peer& bob, Application& app, const std::string& app_name) {
    if (!host.net.host(0, "Alice", true)) return false;
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
    TEST_CASE("N5.1 Command Line: --name, -N<team><name> (the original's), --team-name, -pnum=, --host [port], --join host[:port], --port, --loopback") {
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
        app.map_select().set_selected_index(1);                                         // SMALL
        app.map_select().handle_mouse_down(MapSelectScreen::BTN_FOW_ON_X + 2, MapSelectScreen::BTN_FOW_ON_Y + 2, 1);
        duo.step(200);
        ASSERT_EQ(bob.net.room().map_name, app.map_select().get_maps()[1].filename);
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
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Join;
        cfg.net_address = "127.0.0.1";
        cfg.net_port = host.net.listen_port();
        Application app;                                                                  // no --name: the default in a network game is "Player"
        ASSERT_TRUE(app.init(cfg));
        ASSERT_TRUE(app.network_active());
        ASSERT_FALSE(app.net()->is_host());
        Duo duo{app, host};
        ASSERT_TRUE(duo.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && host.net.can_start(); }, 8000));
        const auto& room = app.map_select().room();
        ASSERT_TRUE(room.networked && !room.is_host && room.my_seat == 1);
        ASSERT_TRUE(room.seats[0].occupied && room.seats[0].name == "Alice" && room.seats[1].occupied && room.seats[1].name == "Player");
        ASSERT_EQ(room.status, std::string(sim::strings::text(sim::strings::kWaitingForHost)));
        // the host's choice arrives; the guest's controls do nothing
        host.net.set_map("TINY.LVL");
        host.net.set_fog(true);
        duo.step(300);
        ASSERT_EQ(app.map_select().get_maps()[static_cast<size_t>(app.map_select().get_selected_index())].filename, "TINY.LVL");
        ASSERT_TRUE(app.map_select().is_fog_of_war_enabled());
        const int32_t idx = app.map_select().get_selected_index();
        app.map_select().handle_mouse_down(MapSelectScreen::BTN_DOWN_X + 3, MapSelectScreen::BTN_DOWN_Y + 3, 1);
        app.map_select().handle_key_down(SDLK_RETURN);
        app.map_select().handle_key_down(SDLK_f);
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

}  // namespace

int main() {
    std::cout << "\n=======================================================\n [SUITE] Network port: the application (names, room, thumbs, start, match)\n"
                 "=======================================================\n";
    run_command_line_tests();
    run_host_tests();
    run_guest_tests();
    std::cout << "\n=======================================================\n Total Test Cases: " << g_test_count << "\n Total Assertions: " << g_assert_count
              << "\n Failed:           " << g_test_failures << "\n=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}
