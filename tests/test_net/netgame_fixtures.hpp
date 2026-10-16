// The fixtures that the parts of the NetGame test share (test_netgame.cpp holds main(); the cases are in test_netgame.cpp and test_netgame_teams.cpp). Inline or in an
// anonymous namespace, so that every part is a self-contained translation unit.
#pragma once
#include "ants_assets/lvl_parser.hpp"
#include "ants_net/netgame.hpp"
#include "ants_net/tcp.hpp"
#include "ants_sim/game_strings.hpp"
#include "ants_sim/movement_tables.hpp"
#include "ants_sim/sim_engine.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "ants_test_paths.hpp"
#include "manual_clock.hpp"
#include "../common/ants_test_pause.hpp"
#include "../common/test_assert.hpp"

using namespace ants;
using namespace ants::net;
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

// One machine: an engine, its NetGame and the little bit of "application" that the room needs (load the map, initialise the simulation, report)
struct Machine {
    sim::SimulationEngine sim;
    NetGame net{sim};
    std::string name;
    bool corrupt_map{false};                // this machine cannot load the map (a different file)
    bool hold_load{false};                  // this machine loads the map but does not report it (a slow disk): the test reports with net.report_loaded(true) when it is time
    std::vector<NetGame::Event> events;
    std::vector<ChatMsg> chats;
    std::vector<uint64_t> drop_ticks;       // the sim tick at which a Drop command was applied
    std::vector<std::pair<uint64_t, Command>> applied;     // every other command that a turn applied on this machine, with the sim tick it was applied at
    uint64_t ticks{0};
    uint32_t loads{0};

    explicit Machine(std::string n) : name(std::move(n)) { net.set_discovery(0); }     // the tests do not announce their rooms on the real network (N3.13 and up ask for it)

    void handle(const NetGame::Event& ev) {
        events.push_back(ev);
        if (ev.type == NetGame::Event::Type::StartRequested) {
            const StartMsg& s = net.start_info();
            ants::assets::LevelData level;
            uint64_t hash = 0;
            const bool ok = !corrupt_map && level.load_lvl(maps_dir() + s.map_name) && hash_file(maps_dir() + s.map_name, hash) && hash == s.map_hash;
            if (ok) {
                sim.set_fog_of_war_enabled(s.fog);
                sim.set_game_mode(s.game_mode());                         // (protocol 17: the Start says the rules, before the first init)
                sim.init(level, s.seed, s.roster);
                for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) sim.set_player_name(p, s.names[p]);
                sim::apply_start_teams(sim, s.teams());                   // (after the names, as the application does: the News Flash names the players)
                ++loads;
            }
            if (hold_load && ok) return;
            net.report_loaded(ok);
        }
    }
    bool saw(NetGame::Event::Type t) const {
        for (const auto& e : events) {
            if (e.type == t) return true;
        }
        return false;
    }
    size_t count(NetGame::Event::Type t) const {
        size_t n = 0;
        for (const auto& e : events) n += e.type == t;
        return n;
    }
};

struct Table {
    std::vector<std::unique_ptr<Machine>> machines;
    uint32_t now{1000};
    bool keep_up{false};                    // run() waits for the loopback after every step (see wait_for_turns)

    Machine& add(const std::string& name) {
        machines.push_back(std::make_unique<Machine>(name));
        Machine& m = *machines.back();
        m.net.set_on_chat([&m](const ChatMsg& c) { m.chats.push_back(c); });
        m.net.set_on_tick([&m]() { ++m.ticks; });
        m.net.set_on_command([&m](const Command& c, const sim::CommandResult&) {
            if (c.type == CommandType::Drop) m.drop_ticks.push_back(m.sim.current_tick());
            else m.applied.emplace_back(m.sim.current_tick(), c);
        });
        return m;
    }
    // One pass over every machine at the current game time (no game time passes)
    void pump() {
        for (auto& m : machines) {
            m->net.update(now);
            for (const auto& ev : m->net.take_events()) m->handle(ev);
        }
    }
    // 10 ms of game time per step with a moment of real time so that the kernel can deliver the loopback bytes
    void run(uint32_t ms, const std::function<void(uint32_t)>& each = {}) {
        const uint32_t end = now + ms;
        while (now < end) {
            now += 10;
            pump();
            if (keep_up) wait_for_turns();
            if (each) each(now);
            ants_test::short_pause();
        }
    }
    // True while a guest has not read a turn that the host (the first machine) has sealed; the host's count is 0 until it plays
    bool turns_unread() const {
        const uint64_t sealed = machines.front()->net.bot_apply_tick(0);
        for (size_t i = 1; i < machines.size(); ++i) {
            const LockstepRunner* r = machines[i]->net.runner();
            if (r != nullptr && r->next_turn_expected() < sealed) return true;
        }
        return false;
    }
    // keep_up. The game clock is virtual and the sockets are real: on a loaded Mac the kernel hands loopback bytes over some milliseconds after they were sent, which is tens of milliseconds of game time
    // at 10 ms for a step of half a millisecond, and a machine's jitter buffer takes that for the lateness of the LINK: it grows at once on a late turn and shrinks only after ten seconds, so a case that holds the
    // buffer, or the lag that follows from it, to a value fails there and nowhere else. With keep_up the table stands still after every step (the game clock where it is, the machines polling) until every guest
    // has read every turn that the host has sealed, which is the link of a loopback on Linux: no lateness. A wait that does not end (a guest that was cut off) gives up after about a second and switches keep_up
    // off, and the case goes on and fails by its own assertions. Turns only: a case that holds an ORDER to the tick that it lands on has the lateness of the order's way to the host (a few orders are allowed to be late).
    void wait_for_turns() {
        for (int i = 0; i < 3000 && turns_unread(); ++i) {
            ants_test::short_pause(std::chrono::microseconds(200));
            pump();
        }
        if (turns_unread()) keep_up = false;
    }
    bool run_until(const std::function<bool()>& cond, uint32_t max_ms) {
        const uint32_t end = now + max_ms;
        while (now < end) {
            if (cond()) return true;
            run(10);
        }
        // The game clock is virtual but the sockets are real: on a busy machine the kernel can be late with a close or with bytes that were sent long ago
        // in game time (the whole budget above can pass in a few hundred real milliseconds). It gets up to two more seconds of real time with the game
        // clock standing still, so that nothing times out meanwhile; a wait that succeeds never gets here.
        for (int i = 0; i < 2000 && !cond(); ++i) {
            pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return cond();
    }
};

// A room with a host ("Alice") and `guests` guests, everybody in it
inline bool make_room(Table& t, uint8_t guests) {
    Machine& host = t.add("Alice");
    if (!host.net.host(0, "Alice", true)) return false;
    host.net.set_map("TINY.LVL");                                            // a room has a map once its host has chosen one (the setup screen lists the Maps folder)
    static const char* names[] = {"Bob", "Carol", "Dave"};
    for (uint8_t i = 0; i < guests; ++i) {
        Machine& g = t.add(names[i]);
        if (!g.net.join("127.0.0.1", host.net.listen_port(), names[i])) return false;
        if (!t.run_until([&]() { return g.net.phase() == NetGame::Phase::Room && g.net.my_seat() == i + 1; }, 3000)) return false;      // (the seats are given in the order in which the Hellos are read: the next guest joins after this one is in)
    }
    // everybody is in the room and every player's thumb has appeared (the host measured every guest and told everybody)
    return t.run_until(
        [&]() {
            for (auto& m : t.machines) {
                if (m->net.phase() != NetGame::Phase::Room) return false;
                for (uint8_t s = 0; s <= guests; ++s) {
                    if (m->net.room().slots[s].state == SlotState::Empty || m->net.room().slots[s].rtt_ms == kRttUnknown) return false;
                }
            }
            return host.net.room().slots[guests].state == SlotState::Client && host.net.can_start();
        },
        8000);
}

inline Command order(uint8_t issuer, uint32_t ant, int16_t x, int16_t y) {
    Command c;
    c.type = CommandType::GroupMove;
    c.issuer = issuer;
    c.tile_x = x;
    c.tile_y = y;
    c.ants = {ant};
    return c;
}

// The first ant of a team in a machine's world
inline uint32_t first_ant(Machine& m, uint8_t player) {
    for (const auto& a : m.sim.get_world_state().ants) {
        if (a.player_id == player) return a.id;
    }
    return 0;
}

// An open ground tile near a team's hill: a goal that the path finder accepts (the tile just outside the hill is not open on every map)
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

inline bool everybody_playing(Table& t) {
    for (auto& m : t.machines) {
        if (m->net.phase() != NetGame::Phase::Playing) return false;
    }
    return true;
}

// The match is under way: everybody plays AND every machine has executed its first turn. The host seals the first turn kMatchStartDelayMs after the match began (protocol 12: the "Get ready to
// play!" dialog of every machine, in which no simulation runs), so this is later than everybody_playing (the Begin) by that much.
inline bool everybody_running(Table& t) {
    if (!everybody_playing(t)) return false;
    for (auto& m : t.machines) {
        if (m->ticks == 0) return false;
    }
    return true;
}
constexpr uint32_t kUntilRunning = 5000 + kMatchStartDelayMs;

inline bool all_equal(Table& t, size_t skip = 99) {
    const sim::StateHash h = t.machines[0]->sim.state_hash();
    for (size_t i = 1; i < t.machines.size(); ++i) {
        if (i != skip && t.machines[i]->sim.state_hash() != h) return false;
    }
    return true;
}

}  // namespace

// The groups of cases, run by main() in test_netgame.cpp in this order
void run_room_tests();
void run_thumb_tests();
void run_start_tests();
void run_match_tests();
void run_lan_tests();
void run_seat_tests();
void run_migration_tests();
void run_reject_tests();
void run_room_chat_tests();
void run_start_delay_tests();
void run_team_tests();
void run_seat_move_tests();
void run_prediction_tests();
