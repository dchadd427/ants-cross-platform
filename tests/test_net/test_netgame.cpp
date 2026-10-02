// Tests of NetGame, the network as the application sees it, over real sockets on the loopback interface: the room (joining, the map and fog the
// host picks, a full room), the start barrier (every machine loads the same map file), a whole match of a host and two guests with commands and chat
// (all simulations bit-identical at the end), a roster of two teams, a guest that leaves (its team is dropped at the same tick everywhere), a host that
// leaves, a machine whose map differs, and joining after the match began.
#include "ants_assets/lvl_parser.hpp"
#include "ants_net/netgame.hpp"
#include "ants_net/tcp.hpp"
#include "ants_sim/game_strings.hpp"
#include "ants_sim/movement_tables.hpp"
#include "ants_sim/sim_engine.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace ants;
using namespace ants::net;
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
                sim.init(level, s.seed, s.roster);
                for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) sim.set_player_name(p, s.names[p]);
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

    Machine& add(const std::string& name) {
        machines.push_back(std::make_unique<Machine>(name));
        Machine& m = *machines.back();
        m.net.set_on_chat([&m](const ChatMsg& c) { m.chats.push_back(c); });
        m.net.set_on_tick([&m]() { ++m.ticks; });
        m.net.set_on_command([&m](const Command& c, const sim::CommandResult&) {
            if (c.type == CommandType::Drop) m.drop_ticks.push_back(m.sim.current_tick());
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
            if (each) each(now);
            std::this_thread::sleep_for(std::chrono::microseconds(300));
        }
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
bool make_room(Table& t, uint8_t guests) {
    Machine& host = t.add("Alice");
    if (!host.net.host(0, "Alice", true)) return false;
    host.net.set_map("TINY.LVL");                                            // a room has a map once its host has chosen one (the setup screen lists the Maps folder)
    static const char* names[] = {"Bob", "Carol", "Dave"};
    for (uint8_t i = 0; i < guests; ++i) {
        Machine& g = t.add(names[i]);
        if (!g.net.join("127.0.0.1", host.net.listen_port(), names[i])) return false;
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

Command order(uint8_t issuer, uint32_t ant, int16_t x, int16_t y) {
    Command c;
    c.type = CommandType::GroupMove;
    c.issuer = issuer;
    c.tile_x = x;
    c.tile_y = y;
    c.ants = {ant};
    return c;
}

// The first ant of a team in a machine's world
uint32_t first_ant(Machine& m, uint8_t player) {
    for (const auto& a : m.sim.get_world_state().ants) {
        if (a.player_id == player) return a.id;
    }
    return 0;
}

// An open ground tile near a team's hill: a goal that the path finder accepts (the tile just outside the hill is not open on every map)
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

bool everybody_playing(Table& t) {
    for (auto& m : t.machines) {
        if (m->net.phase() != NetGame::Phase::Playing) return false;
    }
    return true;
}

bool all_equal(Table& t, size_t skip = 99) {
    const sim::StateHash h = t.machines[0]->sim.state_hash();
    for (size_t i = 1; i < t.machines.size(); ++i) {
        if (i != skip && t.machines[i]->sim.state_hash() != h) return false;
    }
    return true;
}

void run_room_tests() {
    TEST_CASE("N3.1 Room: Guests Join And Get Seats In Arrival Order; The Host Picks The Map And The Fog And Every Guest Follows") {
        Table t;
        ASSERT_TRUE(make_room(t, 2));
        Machine& host = *t.machines[0];
        Machine& bob = *t.machines[1];
        Machine& carol = *t.machines[2];
        ASSERT_TRUE(host.net.is_host());
        ASSERT_EQ(host.net.my_seat(), 0);
        ASSERT_EQ(bob.net.my_seat(), 1);
        ASSERT_EQ(carol.net.my_seat(), 2);
        ASSERT_EQ(bob.net.room().slots[0].state, SlotState::Host);
        ASSERT_EQ(bob.net.room().slots[0].name, "Alice");
        ASSERT_EQ(carol.net.room().slots[1].name, "Bob");
        ASSERT_EQ(carol.net.room().slots[3].state, SlotState::Empty);
        ASSERT_TRUE(host.net.can_start());
        ASSERT_FALSE(bob.net.can_start());                                  // only the host starts
        host.net.set_map("SMALL.LVL");
        host.net.set_fog(true);
        bob.net.set_map("TINY.LVL");                                        // a guest's attempt changes nothing
        bob.net.set_fog(false);
        t.run(300);
        for (Machine* g : {&bob, &carol}) {
            ASSERT_EQ(g->net.room().map_name, "SMALL.LVL");
            ASSERT_TRUE(g->net.room().fog);
        }
        ASSERT_FALSE(bob.net.start_match(1, 1));
        // a guest that leaves frees its seat for everybody
        carol.net.leave();
        ASSERT_EQ(carol.net.phase(), NetGame::Phase::Off);
        ASSERT_TRUE(t.run_until([&]() { return host.net.room().slots[2].state == SlotState::Empty && bob.net.room().slots[2].state == SlotState::Empty; }, 3000));
    } TEST_END();

    TEST_CASE("N3.2 Room: A Full Room Refuses The Fifth Player, A Host Alone Cannot Start, A Wrong Address Fails Cleanly") {
        Table t;
        ASSERT_TRUE(make_room(t, 3));
        Machine& host = *t.machines[0];
        Machine& fifth = t.add("Eve");
        ASSERT_TRUE(fifth.net.join("127.0.0.1", host.net.listen_port(), "Eve"));
        ASSERT_TRUE(t.run_until([&]() { return fifth.net.phase() == NetGame::Phase::Failed; }, 5000));
        ASSERT_TRUE(fifth.saw(NetGame::Event::Type::Failed));
        ASSERT_TRUE(fifth.net.status_text().find("full") != std::string::npos);
        // nobody else noticed
        for (size_t i = 0; i < 4; ++i) ASSERT_EQ(t.machines[i]->net.phase(), NetGame::Phase::Room);
        Table solo;
        Machine& lonely = solo.add("Alice");
        ASSERT_TRUE(lonely.net.host(0, "Alice", true));
        ASSERT_FALSE(lonely.net.can_start());
        ASSERT_FALSE(lonely.net.start_match(1, 1));
        // nobody listens on that port
        Machine& lost = solo.add("Zed");
        ASSERT_TRUE(lost.net.join("127.0.0.1", 1, "Zed"));
        ASSERT_TRUE(solo.run_until([&]() { return lost.net.phase() == NetGame::Phase::Failed; }, 5000));
        ASSERT_FALSE(lost.net.status_text().empty());
        // host() twice and join() while hosting are refused
        ASSERT_FALSE(lonely.net.host(0, "Alice", true));
        ASSERT_FALSE(lonely.net.join("127.0.0.1", 5, "x"));
    } TEST_END();
}

void run_thumb_tests() {
    TEST_CASE("N3.9 Thumbs: Every Seat Shows Its Connection Quality; A Guest Who Never Answers Keeps The Question Mark And Holds Up START") {
        Table t;
        Machine& host = t.add("Alice");
        ASSERT_TRUE(host.net.host(0, "Alice", true));
        host.net.set_map("TINY.LVL");
        // the host alone: its own thumb is good, START needs a second player
        t.run(200);
        ASSERT_EQ(host.net.seat_quality(0), LinkQuality::Good);
        ASSERT_EQ(host.net.seat_quality(1), LinkQuality::Unknown);          // an empty seat shows nothing
        ASSERT_FALSE(host.net.can_start());
        ASSERT_EQ(host.net.status_text(), std::string(sim::strings::text(sim::strings::kPressStart)));
        // a real guest is measured within moments and everybody sees a green thumb
        Machine& bob = t.add("Bob");
        ASSERT_TRUE(bob.net.join("127.0.0.1", host.net.listen_port(), "Bob"));
        ASSERT_TRUE(t.run_until([&]() { return host.net.can_start() && bob.net.room().slots[1].rtt_ms != kRttUnknown; }, 8000));
        for (Machine* m : {&host, &bob}) {
            ASSERT_EQ(m->net.seat_quality(0), LinkQuality::Good);
            ASSERT_EQ(m->net.seat_quality(1), LinkQuality::Good);
            ASSERT_TRUE(m->net.room().slots[1].rtt_ms < 1200);
        }
        ASSERT_EQ(bob.net.status_text(), std::string(sim::strings::text(sim::strings::kWaitingForHost)));
        // a guest that says Hello but never answers a ping: its seat shows the question mark and the host cannot start with it
        Table raw;
        Machine& h2 = raw.add("Host");
        ASSERT_TRUE(h2.net.host(0, "Host", true));
        h2.net.set_map("TINY.LVL");
        auto mute = TcpConnection::connect("127.0.0.1", h2.net.listen_port());
        ASSERT_TRUE(mute != nullptr);
        HelloMsg hello;
        hello.name = "Mute";
        bool sent = false;
        raw.run_until(
            [&]() {
                std::vector<uint8_t> nothing;
                mute->poll(nothing);
                if (!sent && mute->is_open()) sent = mute->send(encode(hello));
                return h2.net.room().slots[1].state == SlotState::Client;
            },
            5000);
        ASSERT_TRUE(h2.net.room().slots[1].state == SlotState::Client);
        raw.run(3000, [&](uint32_t) {
            std::vector<uint8_t> msg;
            while (mute->poll(msg)) {}                                      // reads the room and the pings, answers nothing
        });
        ASSERT_EQ(h2.net.seat_quality(1), LinkQuality::Unknown);            // no thumb: connected but not measured
        ASSERT_FALSE(h2.net.can_start());                                   // "when all players' thumbs have appeared"
        mute->close();
        ASSERT_TRUE(raw.run_until([&]() { return h2.net.room().slots[1].state == SlotState::Empty; }, 5000));
    } TEST_END();

    TEST_CASE("N3.10 Thumbs: The Quality Tiers Are The Original's (below 1200 ms good, below 1800 ms ok, more bad, no answer yet unknown)") {
        ASSERT_EQ(link_quality(0), LinkQuality::Good);
        ASSERT_EQ(link_quality(1199), LinkQuality::Good);
        ASSERT_EQ(link_quality(1200), LinkQuality::Ok);
        ASSERT_EQ(link_quality(1799), LinkQuality::Ok);
        ASSERT_EQ(link_quality(1800), LinkQuality::Bad);
        ASSERT_EQ(link_quality(60000), LinkQuality::Bad);
        ASSERT_EQ(link_quality(kRttUnknown), LinkQuality::Unknown);
        // the strings of the original's setup screen, by id
        ASSERT_EQ(std::string(sim::strings::text(sim::strings::kConnectingToHost)), "Trying to connect to the host...");
        ASSERT_EQ(std::string(sim::strings::text(sim::strings::kTroubleConnecting)), "Having trouble connecting to host...");
        ASSERT_EQ(std::string(sim::strings::text(sim::strings::kUnableToConnect)), "Unable to connect to host, recommend you quit...");
        ASSERT_EQ(std::string(sim::strings::text(sim::strings::kWaitingForHost)), "Waiting for the host to start the game...");
        ASSERT_EQ(std::string(sim::strings::text(sim::strings::kPressStart)), "Press START when all players' thumbs have appeared.");
    } TEST_END();
}

void run_start_tests() {
    TEST_CASE("N3.3 Start Barrier: Every Machine Loads The Same Map With The Same Seed And Roster, Then The Match Begins Everywhere") {
        Table t;
        ASSERT_TRUE(make_room(t, 2));
        Machine& host = *t.machines[0];
        host.net.set_map("TREASURE.LVL");
        t.run(200);
        uint64_t hash = 0;
        ASSERT_TRUE(hash_file(maps_dir() + "TREASURE.LVL", hash));
        ASSERT_TRUE(host.net.start_match(4242, hash));
        ASSERT_EQ(host.net.phase(), NetGame::Phase::Loading);
        ASSERT_TRUE(t.run_until([&]() { return everybody_playing(t); }, 5000));
        for (auto& m : t.machines) {
            ASSERT_EQ(m->loads, 1u);
            ASSERT_EQ(m->net.start_info().seed, 4242u);
            ASSERT_EQ(m->net.start_info().map_name, "TREASURE.LVL");
            ASSERT_EQ(m->net.start_info().roster, 0x07);
            ASSERT_EQ(m->sim.roster_mask(), 0x07);                          // seats 0, 1 and 2 play: the fourth team does not exist
            ASSERT_TRUE(m->saw(NetGame::Event::Type::Begun));
            ASSERT_EQ(m->net.start_info().names[m->net.my_seat()], m->name);
        }
        ASSERT_EQ(host.sim.grid().anthills().size(), 3u);
        ASSERT_TRUE(all_equal(t));                                          // same start state before the first turn
        // the door is closed: nobody joins a running match
        Machine& late = t.add("Late");
        ASSERT_TRUE(late.net.join("127.0.0.1", host.net.listen_port(), "Late"));
        ASSERT_TRUE(t.run_until([&]() { return late.net.phase() == NetGame::Phase::Failed; }, 5000));
    } TEST_END();

    TEST_CASE("N3.4 Start Barrier: A Machine That Cannot Load The Map Cancels The Start For Everybody; The Room Works Again Afterwards") {
        Table t;
        ASSERT_TRUE(make_room(t, 2));
        Machine& host = *t.machines[0];
        Machine& carol = *t.machines[2];
        host.net.set_map("SMALL.LVL");
        uint64_t hash = 0;
        ASSERT_TRUE(hash_file(maps_dir() + "SMALL.LVL", hash));
        carol.corrupt_map = true;                                            // her file differs
        ASSERT_TRUE(host.net.start_match(7, hash));
        ASSERT_TRUE(t.run_until([&]() { return host.saw(NetGame::Event::Type::Cancelled); }, 5000));
        ASSERT_TRUE(t.run_until([&]() {
            for (auto& m : t.machines) {
                if (m->net.phase() != NetGame::Phase::Room) return false;
            }
            return true;
        }, 5000));
        ASSERT_FALSE(host.net.status_text().empty());
        carol.corrupt_map = false;                                           // fixed: the second try works
        ASSERT_TRUE(host.net.start_match(8, hash));
        ASSERT_TRUE(t.run_until([&]() { return everybody_playing(t); }, 5000));
        ASSERT_TRUE(all_equal(t));
    } TEST_END();
}

void run_match_tests() {
    TEST_CASE("N3.5 Match: Three Machines Play 60 Seconds With Commands, Predicted Acknowledgements And Chat; All Simulations End Identical") {
        Table t;
        ASSERT_TRUE(make_room(t, 2));
        Machine& host = *t.machines[0];
        host.net.set_map("SMALL.LVL");
        uint64_t hash = 0;
        ASSERT_TRUE(hash_file(maps_dir() + "SMALL.LVL", hash));
        ASSERT_TRUE(host.net.start_match(99, hash));
        ASSERT_TRUE(t.run_until([&]() { return everybody_playing(t); }, 5000));
        int predicted_acks = 0;
        int orders = 0;
        uint32_t next_order_ms = t.now + 500;
        t.run(60000, [&](uint32_t now) {
            if (now < next_order_ms) return;
            next_order_ms = now + 700;
            for (uint8_t seat = 0; seat < 3; ++seat) {
                Machine& m = *t.machines[seat];
                // an ant of the player's own team, sent somewhere on the 40 x 40 map
                std::vector<uint32_t> mine;
                for (const auto& a : m.sim.get_world_state().ants) {
                    if (a.player_id == seat) mine.push_back(a.id);
                }
                if (mine.empty()) continue;
                const uint32_t pick = mine[(now / 700 + seat) % mine.size()];
                const sim::CommandResult r = m.net.submit(order(seat, pick, static_cast<int16_t>((now / 10 + seat * 7) % 40), static_cast<int16_t>((now / 30 + seat * 11) % 40)));
                ASSERT_EQ(r.status, sim::CommandResult::Status::Applied);
                ++orders;
                predicted_acks += r.ack_ant != 0 ? 1 : 0;
            }
        });
        ASSERT_TRUE(orders > 150);
        ASSERT_TRUE(predicted_acks > orders / 2);                            // the click feedback is immediate for most orders
        // chat: a guest's text comes back to everybody, the host's too, the sender is stamped by the connection
        t.machines[1]->net.chat("hello from Bob", false);
        host.net.chat("hello from Alice", true);
        t.run(1000);
        for (auto& m : t.machines) {
            bool bob = false;
            bool alice = false;
            for (const auto& c : m->chats) {
                if (c.text == "hello from Bob") bob = c.sender == 1 && !c.team;
                if (c.text == "hello from Alice") alice = c.sender == 0 && c.team;
            }
            ASSERT_TRUE(bob && alice);
        }
        // every machine ran the same number of ticks at 20 Hz (60 s + 1 s = about 1200 - 1220) and the match is still identical
        for (auto& m : t.machines) ASSERT_TRUE(m->ticks > 1150 && m->ticks < 1300);
        host.net.freeze();
        t.run(3000);
        ASSERT_TRUE(all_equal(t));
        for (auto& m : t.machines) ASSERT_FALSE(m->net.desynced());
        ASSERT_EQ(t.machines[0]->sim.current_tick(), t.machines[1]->sim.current_tick());
    } TEST_END();

    TEST_CASE("N3.6 Match: A Guest Who Leaves Is Dropped At The Same Tick On Every Machine; The Others Play On, Identical") {
        Table t;
        ASSERT_TRUE(make_room(t, 2));
        Machine& host = *t.machines[0];
        host.net.set_map("SMALL.LVL");
        uint64_t hash = 0;
        ASSERT_TRUE(hash_file(maps_dir() + "SMALL.LVL", hash));
        ASSERT_TRUE(host.net.start_match(5, hash));
        ASSERT_TRUE(t.run_until([&]() { return everybody_playing(t); }, 5000));
        t.run(5000);
        t.machines[2]->net.leave();                                          // Carol quits
        ASSERT_EQ(t.machines[2]->net.phase(), NetGame::Phase::Off);
        t.run(5000);
        for (size_t i = 0; i < 2; ++i) {
            Machine& m = *t.machines[i];
            ASSERT_EQ(m.count(NetGame::Event::Type::PlayerLeft), 1u);
            ASSERT_TRUE(m.sim.is_player_dropped(2));
            ASSERT_FALSE(m.sim.is_player_dropped(0));
            ASSERT_FALSE(m.sim.is_player_dropped(1));
            ASSERT_EQ(m.drop_ticks.size(), 1u);
        }
        ASSERT_EQ(host.drop_ticks[0], t.machines[1]->drop_ticks[0]);         // the same tick
        ASSERT_TRUE(host.net.turns_executed() > 170);                        // the game went on (10 s = about 200 turns of 50 ms)
        host.net.freeze();
        t.run(2000);
        ASSERT_TRUE(all_equal(t, 2));
        size_t ants_of_carol = 0;
        for (const auto& a : host.sim.get_world_state().ants) ants_of_carol += a.player_id == 2;
        ASSERT_EQ(ants_of_carol, 0u);
    } TEST_END();

    TEST_CASE("N3.7 Host Migration: When The Host Leaves A Two-Player Match The Guest Takes Over At Once; The Drop Ends The Match, The Guest Has Won (0x100d172)") {
        Table t;
        ASSERT_TRUE(make_room(t, 1));
        Machine& host = *t.machines[0];
        host.net.set_map("TINY.LVL");
        uint64_t hash = 0;
        ASSERT_TRUE(hash_file(maps_dir() + "TINY.LVL", hash));
        ASSERT_TRUE(host.net.start_match(3, hash));
        ASSERT_TRUE(t.run_until([&]() { return everybody_playing(t); }, 5000));
        t.run(2000);
        host.net.leave();
        ASSERT_EQ(host.net.phase(), NetGame::Phase::Off);
        Machine& bob = *t.machines[1];
        ASSERT_TRUE(t.run_until([&]() { return bob.net.is_host(); }, 5000));         // nobody to ask: the guest is the host at once
        ASSERT_EQ(bob.net.phase(), NetGame::Phase::Playing);
        ASSERT_EQ(bob.net.host_seat(), 1);
        ASSERT_EQ(bob.count(NetGame::Event::Type::HostChanged), 1u);
        ASSERT_FALSE(bob.saw(NetGame::Event::Type::HostLeft));
        ASSERT_TRUE(t.run_until([&]() { return bob.count(NetGame::Event::Type::PlayerLeft) == 1; }, 3000));      // the old host's team drops out
        ASSERT_TRUE(bob.sim.is_player_dropped(0));
        ASSERT_FALSE(bob.sim.is_player_dropped(1));
        ASSERT_TRUE(bob.sim.is_match_over());                                        // no team is left besides the guest: the drop-out decides the match
        ASSERT_EQ(bob.sim.quitter(), sim::NO_QUITTER);
        ASSERT_TRUE(bob.sim.get_world_state().match_result.is_winner(1));
    } TEST_END();

    TEST_CASE("N3.8 Match: Two Players On A Four-Player Map Have Two Teams Only, And The Command Sink Refuses What It Must") {
        Table t;
        ASSERT_TRUE(make_room(t, 1));
        Machine& host = *t.machines[0];
        host.net.set_map("SMALL.LVL");
        uint64_t hash = 0;
        ASSERT_TRUE(hash_file(maps_dir() + "SMALL.LVL", hash));
        // before the match nothing can be submitted
        ASSERT_EQ(host.net.submit(order(0, 1, 5, 5)).status, sim::CommandResult::Status::Ignored);
        ASSERT_TRUE(host.net.start_match(11, hash));
        ASSERT_TRUE(t.run_until([&]() { return everybody_playing(t); }, 5000));
        for (auto& m : t.machines) {
            ASSERT_EQ(m->sim.roster_mask(), 0x03);
            ASSERT_EQ(m->sim.grid().anthills().size(), 2u);
            for (const auto& a : m->sim.get_world_state().ants) ASSERT_TRUE(a.player_id < 2);
        }
        // the system command cannot be submitted through the sink; a group order is stamped with the local seat whatever it says
        Machine& bob = *t.machines[1];
        Command drop;
        drop.type = CommandType::Drop;
        drop.issuer = 0;
        ASSERT_EQ(bob.net.submit(drop).status, sim::CommandResult::Status::Ignored);
        const uint32_t bobs_ant = first_ant(bob, 1);
        const auto* bobs_hill = bob.sim.grid().find_anthill(1);
        ASSERT_TRUE(bobs_hill != nullptr);
        const int16_t goal_x = static_cast<int16_t>(bobs_hill->x + 4);      // the idle spot outside the hill: always open ground
        const int16_t goal_y = static_cast<int16_t>(bobs_hill->y + 4);
        Command forged = order(0, bobs_ant, goal_x, goal_y);                 // claims to be the host
        const sim::CommandResult r = bob.net.submit(forged);
        ASSERT_EQ(r.status, sim::CommandResult::Status::Applied);
        ASSERT_EQ(r.ack_ant, bobs_ant);                                      // predicted for Bob's own ant
        t.run(2000);
        ASSERT_EQ(host.sim.get_unit(bobs_ant).orig_order, sim::AntUnit::kOrderMove);       // the order reached the host's simulation as Bob's
        ASSERT_TRUE(host.sim.get_unit(bobs_ant).orig_order_tile == bob.sim.get_unit(bobs_ant).orig_order_tile);   // and is the same on both machines
        host.net.freeze();
        t.run(1500);
        ASSERT_TRUE(all_equal(t));
    } TEST_END();
}


// The open room on the local network (lan.hpp): the browser listens on a private port and the room announces itself to this machine only
void run_lan_tests() {
    TEST_CASE("N3.13 LAN: An open room announces its host, map and players, follows every change, and is gone when the match starts") {
        auto browser = LanBrowser::open(0);
        ASSERT_TRUE(browser != nullptr);
        Table t;
        Machine& host = t.add("Alice");
        host.net.set_discovery(browser->port(), true);
        host.net.set_game_version("v-test");
        ASSERT_TRUE(host.net.host(0, "Alice", true));
        host.net.set_map("TINY.LVL");
        ASSERT_TRUE(t.run_until([&]() { browser->update(t.now); return browser->rooms().size() == 1; }, 5000));
        const LanRoom room = browser->rooms()[0];
        ASSERT_EQ(room.info.host_name, "Alice");
        ASSERT_EQ(room.info.map_name, "TINY.LVL");
        ASSERT_EQ(room.info.players, 1);
        ASSERT_EQ(room.info.seats, 4);
        ASSERT_EQ(room.info.tcp_port, host.net.listen_port());
        ASSERT_EQ(room.info.version, "v-test");
        ASSERT_EQ(room.info.protocol, kProtocolVersion);
        ASSERT_TRUE(room.compatible);
        ASSERT_TRUE(host.net.announcing());
        // a guest joins at the address and port the list gave: the next announcement says two players
        Machine& bob = t.add("Bob");
        ASSERT_TRUE(bob.net.join(room.address, room.info.tcp_port, "Bob"));
        ASSERT_TRUE(t.run_until([&]() { browser->update(t.now); return !browser->rooms().empty() && browser->rooms()[0].info.players == 2; }, 5000));
        ASSERT_EQ(browser->rooms().size(), size_t{1});                       // the same room, updated in place
        host.net.set_map("SMALL.LVL");                                       // the host picks another map
        ASSERT_TRUE(t.run_until([&]() { browser->update(t.now); return !browser->rooms().empty() && browser->rooms()[0].info.map_name == "SMALL.LVL"; }, 5000));
        uint64_t hash = 0;
        ASSERT_TRUE(hash_file(maps_dir() + "SMALL.LVL", hash));
        ASSERT_TRUE(t.run_until([&]() { browser->update(t.now); return host.net.can_start(); }, 5000));      // START waits for every player's thumb
        ASSERT_TRUE(host.net.start_match(5, hash));                           // START: a running match is offered to nobody
        ASSERT_TRUE(t.run_until([&]() { browser->update(t.now); return browser->rooms().empty(); }, 5000));
        ASSERT_FALSE(host.net.announcing());
        ASSERT_TRUE(t.run_until([&]() { return everybody_playing(t); }, 5000));
        t.run(4000, [&](uint32_t now) { browser->update(now); });
        ASSERT_TRUE(browser->rooms().empty());                                // and it stays away during the match
        ASSERT_FALSE(host.net.announcing());
        ASSERT_FALSE(bob.net.announcing());                                   // a guest never announces
    } TEST_END();

    TEST_CASE("N3.14 LAN: A start that fails puts the room back on the list; leaving removes it at once") {
        auto browser = LanBrowser::open(0);
        Table t;
        Machine& host = t.add("Alice");
        host.net.set_discovery(browser->port(), true);
        ASSERT_TRUE(host.net.host(0, "Alice", true));
        host.net.set_map("TINY.LVL");
        Machine& bob = t.add("Bob");
        ASSERT_TRUE(bob.net.join("127.0.0.1", host.net.listen_port(), "Bob"));
        ASSERT_TRUE(t.run_until([&]() { browser->update(t.now); return !browser->rooms().empty() && browser->rooms()[0].info.players == 2; }, 5000));
        bob.corrupt_map = true;                                               // his file differs: the start fails for everybody
        uint64_t hash = 0;
        ASSERT_TRUE(hash_file(maps_dir() + "TINY.LVL", hash));
        ASSERT_TRUE(t.run_until([&]() { browser->update(t.now); return host.net.can_start(); }, 5000));
        ASSERT_TRUE(host.net.start_match(1, hash));
        ASSERT_TRUE(t.run_until([&]() { browser->update(t.now); return host.saw(NetGame::Event::Type::Cancelled); }, 5000));
        ASSERT_TRUE(t.run_until([&]() { browser->update(t.now); return host.net.phase() == NetGame::Phase::Room && browser->rooms().size() == 1; }, 5000));
        ASSERT_TRUE(host.net.announcing());                                   // back in the room: open again
        host.net.leave();                                                     // the host closes the room: the goodbye goes out at once
        ASSERT_TRUE(t.run_until([&]() { browser->update(t.now); return browser->rooms().empty(); }, 2000));
    } TEST_END();

    TEST_CASE("N3.15 LAN: A room with the discovery turned off is never announced") {
        auto browser = LanBrowser::open(0);
        Table t;
        Machine& host = t.add("Alice");
        host.net.set_discovery(0);
        ASSERT_TRUE(host.net.host(0, "Alice", true));
        host.net.set_map("TINY.LVL");
        t.run(3000, [&](uint32_t now) { browser->update(now); });
        ASSERT_TRUE(browser->rooms().empty());
        ASSERT_FALSE(host.net.announcing());
        ASSERT_EQ(host.net.phase(), NetGame::Phase::Room);                    // the room itself works
    } TEST_END();
}

// The four colours of a match on one machine (the start scripts): every window asks for its own seat
void run_seat_tests() {
    TEST_CASE("N3.16 Seats: Four Machines Ask For Their Colour In A Scrambled Order And Sit Where They Asked; The Match Gives Them Their Names And Teams") {
        Table t;
        Machine& host = t.add("Alice");
        ASSERT_TRUE(host.net.host(0, "Alice", true));
        host.net.set_map("TINY.LVL");
        Machine& dora = t.add("Dora");
        Machine& bob = t.add("Bob");
        Machine& carl = t.add("Carl");
        ASSERT_TRUE(dora.net.join("127.0.0.1", host.net.listen_port(), "Dora", 3));          // black
        ASSERT_TRUE(bob.net.join("127.0.0.1", host.net.listen_port(), "Bob", 1));            // red
        ASSERT_TRUE(carl.net.join("127.0.0.1", host.net.listen_port(), "Carl", 2));          // blue
        ASSERT_TRUE(t.run_until([&]() { return host.net.can_start(); }, 8000));
        ASSERT_EQ(host.net.my_seat(), 0);
        ASSERT_EQ(bob.net.my_seat(), 1);
        ASSERT_EQ(carl.net.my_seat(), 2);
        ASSERT_EQ(dora.net.my_seat(), 3);
        const RoomMsg& room = host.net.room();
        ASSERT_EQ(room.slots[0].name, "Alice");
        ASSERT_EQ(room.slots[1].name, "Bob");
        ASSERT_EQ(room.slots[2].name, "Carl");
        ASSERT_EQ(room.slots[3].name, "Dora");
        uint64_t hash = 0;
        ASSERT_TRUE(hash_file(maps_dir() + "TINY.LVL", hash));
        ASSERT_TRUE(host.net.start_match(11, hash));
        ASSERT_TRUE(t.run_until([&]() { return everybody_playing(t); }, 8000));
        for (auto& m : t.machines) {                                                          // every machine knows who plays which team
            ASSERT_EQ(m->sim.get_player_name(0), "Alice");
            ASSERT_EQ(m->sim.get_player_name(1), "Bob");
            ASSERT_EQ(m->sim.get_player_name(2), "Carl");
            ASSERT_EQ(m->sim.get_player_name(3), "Dora");
        }
        ASSERT_TRUE(all_equal(t));
    } TEST_END();
}

void run_migration_tests() {
    TEST_CASE("N3.9 Host Migration: The Host Leaves A Three-Player Match; The Lowest Guest Takes Over, The Other Follows, Orders And Chat Go Through The New Host, Both Stay Identical") {
        Table t;
        ASSERT_TRUE(make_room(t, 2));
        Machine& host = *t.machines[0];
        Machine& bob = *t.machines[1];
        Machine& carol = *t.machines[2];
        host.net.set_map("SMALL.LVL");
        uint64_t hash = 0;
        ASSERT_TRUE(hash_file(maps_dir() + "SMALL.LVL", hash));
        // the guests know where to reach each other: the host passes on what the guests announced
        ASSERT_TRUE(host.net.start_match(5, hash));
        ASSERT_TRUE(host.net.start_info().endpoints[0].address.empty());
        ASSERT_TRUE(host.net.start_info().endpoints[1].address == "127.0.0.1" && host.net.start_info().endpoints[1].port == bob.net.peer_port());
        ASSERT_TRUE(host.net.start_info().endpoints[2].address == "127.0.0.1" && host.net.start_info().endpoints[2].port == carol.net.peer_port());
        ASSERT_TRUE(bob.net.peer_port() != 0 && carol.net.peer_port() != 0 && bob.net.peer_port() != carol.net.peer_port());
        ASSERT_TRUE(t.run_until([&]() { return everybody_playing(t); }, 5000));
        t.run(4000);
        bool electing_seen = false;
        host.net.leave();
        ASSERT_TRUE(t.run_until([&]() {
            electing_seen = electing_seen || carol.net.electing();
            return bob.net.is_host() && carol.net.host_seat() == 1;
        }, 8000));
        ASSERT_TRUE(electing_seen || carol.net.host_seat() == 1);                   // (the election may be over within a frame)
        ASSERT_FALSE(carol.net.is_host());
        ASSERT_EQ(carol.count(NetGame::Event::Type::HostChanged), 1u);
        ASSERT_EQ(bob.count(NetGame::Event::Type::HostChanged), 1u);
        for (const auto& e : carol.events) {
            if (e.type == NetGame::Event::Type::HostChanged) ASSERT_EQ(e.seat, 1);
        }
        ASSERT_FALSE(carol.saw(NetGame::Event::Type::HostLeft));
        // the old host drops out at the same tick on both machines
        ASSERT_TRUE(t.run_until([&]() { return bob.count(NetGame::Event::Type::PlayerLeft) == 1 && carol.count(NetGame::Event::Type::PlayerLeft) == 1; }, 5000));
        ASSERT_TRUE(bob.sim.is_player_dropped(0) && carol.sim.is_player_dropped(0));
        ASSERT_EQ(bob.drop_ticks.size(), 1u);
        ASSERT_EQ(bob.drop_ticks[0], carol.drop_ticks[0]);
        // Carol's order goes through Bob and takes effect on both machines
        const uint32_t ant = first_ant(carol, 2);
        int16_t goal_x = 0, goal_y = 0;
        ASSERT_TRUE(open_goal_near_hill(carol.sim, 2, goal_x, goal_y));
        const sim::CommandResult r = carol.net.submit(order(2, ant, goal_x, goal_y));
        ASSERT_EQ(r.status, sim::CommandResult::Status::Applied);
        t.run(2000);
        ASSERT_EQ(bob.sim.get_unit(ant).orig_order, sim::AntUnit::kOrderMove);
        ASSERT_EQ(carol.sim.get_unit(ant).orig_order, sim::AntUnit::kOrderMove);
        // chat both ways, the sender stamped by the connection
        carol.net.chat("still here", false);
        bob.net.chat("I host now", true);
        t.run(1000);
        for (Machine* m : {&bob, &carol}) {
            bool a = false, b = false;
            for (const ChatMsg& c : m->chats) {
                if (c.text == "still here") a = c.sender == 2 && !c.team;
                if (c.text == "I host now") b = c.sender == 1 && c.team;
            }
            ASSERT_TRUE(a && b);
        }
        bob.net.freeze();
        t.run(3000);
        ASSERT_TRUE(bob.sim.state_hash() == carol.sim.state_hash());
        ASSERT_EQ(bob.sim.current_tick(), carol.sim.current_tick());
        ASSERT_FALSE(bob.net.desynced() || carol.net.desynced());
    } TEST_END();

    TEST_CASE("N3.10 Host Migration: The Host And The Next Seat Leave Together; The Third Guest Takes Over And The Fourth Follows") {
        Table t;
        ASSERT_TRUE(make_room(t, 3));
        Machine& host = *t.machines[0];
        host.net.set_map("SMALL.LVL");
        uint64_t hash = 0;
        ASSERT_TRUE(hash_file(maps_dir() + "SMALL.LVL", hash));
        ASSERT_TRUE(host.net.start_match(6, hash));
        ASSERT_TRUE(t.run_until([&]() { return everybody_playing(t); }, 5000));
        t.run(4000);
        Machine& carol = *t.machines[2];
        Machine& dave = *t.machines[3];
        host.net.leave();
        t.machines[1]->net.leave();
        ASSERT_TRUE(t.run_until([&]() { return carol.net.is_host() && dave.net.host_seat() == 2; }, 10000));
        ASSERT_TRUE(t.run_until([&]() { return carol.count(NetGame::Event::Type::PlayerLeft) == 2 && dave.count(NetGame::Event::Type::PlayerLeft) == 2; }, 5000));
        for (Machine* m : {&carol, &dave}) {
            ASSERT_TRUE(m->sim.is_player_dropped(0) && m->sim.is_player_dropped(1));
            ASSERT_FALSE(m->sim.is_player_dropped(2) || m->sim.is_player_dropped(3));
            ASSERT_EQ(m->count(NetGame::Event::Type::HostChanged), 1u);
        }
        ASSERT_EQ(carol.drop_ticks.size(), 2u);
        ASSERT_TRUE(carol.drop_ticks == dave.drop_ticks);                           // both drops at the same ticks on both machines
        const uint32_t before = dave.net.turns_executed();
        t.run(3000);
        ASSERT_TRUE(dave.net.turns_executed() > before + 40);                  // 3 s: sixty turns of 50 ms
        carol.net.freeze();
        t.run(3000);
        ASSERT_TRUE(carol.sim.state_hash() == dave.sim.state_hash());
    } TEST_END();

    TEST_CASE("N3.11 Host Migration: Once The Match Is Over A Host That Leaves Is No Reason To Look For A New One") {
        Table t;
        ASSERT_TRUE(make_room(t, 2));
        Machine& host = *t.machines[0];
        host.net.set_map("SMALL.LVL");
        uint64_t hash = 0;
        ASSERT_TRUE(hash_file(maps_dir() + "SMALL.LVL", hash));
        ASSERT_TRUE(host.net.start_match(7, hash));
        ASSERT_TRUE(t.run_until([&]() { return everybody_playing(t); }, 5000));
        t.run(3000);
        for (auto& m : t.machines) m->net.freeze();                                 // the application does this when the match is over
        t.run(1000);
        host.net.leave();
        t.run(4000);
        for (size_t i = 1; i < 3; ++i) {
            ASSERT_FALSE(t.machines[i]->net.electing());
            ASSERT_FALSE(t.machines[i]->net.is_host());
            ASSERT_FALSE(t.machines[i]->saw(NetGame::Event::Type::HostChanged));
            ASSERT_EQ(t.machines[i]->net.phase(), NetGame::Phase::Playing);
        }
    } TEST_END();

    TEST_CASE("N3.12 Host Migration: A Stranger On A Guest's Port Cannot Take A Seat (wrong seat, taken seat, garbage); The Match Does Not Notice") {
        Table t;
        ASSERT_TRUE(make_room(t, 2));
        Machine& host = *t.machines[0];
        host.net.set_map("SMALL.LVL");
        uint64_t hash = 0;
        ASSERT_TRUE(hash_file(maps_dir() + "SMALL.LVL", hash));
        ASSERT_TRUE(host.net.start_match(8, hash));
        ASSERT_TRUE(t.run_until([&]() { return everybody_playing(t); }, 5000));
        t.run(2000);
        Machine& carol = *t.machines[2];                                            // seat 2: only seat 1 may connect to it
        const std::vector<std::vector<uint8_t>> claims = {encode(PeerHelloMsg{3}),   // no such player
                                                          encode(PeerHelloMsg{1}),   // seat 1 is linked already
                                                          encode(PeerHelloMsg{2}),   // its own seat
                                                          encode(PeerHelloMsg{0}),   // the host is no guest
                                                          std::vector<uint8_t>{250, 1, 2, 3}};
        for (const auto& claim : claims) {
            auto stranger = TcpConnection::connect("127.0.0.1", carol.net.peer_port());
            ASSERT_TRUE(stranger != nullptr);
            bool sent = false;
            ASSERT_TRUE(t.run_until([&]() {
                std::vector<uint8_t> nothing;
                stranger->poll(nothing);
                if (stranger->is_open() && !sent) sent = stranger->send(claim);
                return sent;
            }, 3000));
            ASSERT_TRUE(t.run_until([&]() {
                std::vector<uint8_t> nothing;
                stranger->poll(nothing);
                return !stranger->is_open();
            }, 3000));                                                                // thrown out
        }
        t.run(2000);
        for (auto& m : t.machines) {
            ASSERT_FALSE(m->net.electing());
            ASSERT_FALSE(m->saw(NetGame::Event::Type::HostChanged));
            ASSERT_FALSE(m->net.desynced());
        }
        host.net.freeze();
        t.run(3000);
        ASSERT_TRUE(all_equal(t));
    } TEST_END();
}

// A client that is refused: every reason of a Reject ends the join with its own text on the screen
void run_reject_tests() {
    TEST_CASE("N3.17 Rejections (Protocol 10): Dropped Is The Original's Text For A Dropped Machine (String 94), RejoinFailed And Superseded Say What Happened, Each Ends The Join With Its Own Line; The Texts Of The Older Reasons Are What They Were") {
        struct Case {
            RejectReason reason;
            std::string text;
        };
        const std::vector<Case> cases = {
            {RejectReason::Full, "The room is full."},
            {RejectReason::VersionMismatch, "This version cannot play with the host's version."},
            {RejectReason::MatchRunning, "The match has already started."},
            {RejectReason::Kicked, sim::strings::text(sim::strings::kDroppedFromGame)},
            {RejectReason::BadRequest, "The host refused the connection."},
            {RejectReason::NoSuchRoom, "There is no such room on this server."},
            {RejectReason::Dropped, sim::strings::text(sim::strings::kDroppedFromGame)},      // the original's one text for a machine that was dropped: "Sorry, you have been dropped from the game.  Hit OK to exit the program."
            {RejectReason::RejoinFailed, "The game could not be rejoined."},
            {RejectReason::Superseded, "This game was taken over by another window."},
        };
        ASSERT_EQ(sim::strings::text(sim::strings::kDroppedFromGame), std::string("Sorry, you have been dropped from the game.  Hit OK to exit the program."));
        for (const Case& c : cases) {
            auto listener = TcpListener::listen(0, true);
            ASSERT_TRUE(listener != nullptr);
            sim::SimulationEngine sim;
            NetGame net(sim);
            net.set_discovery(0);
            ASSERT_TRUE(net.join("127.0.0.1", listener->port(), "Bob", 255, "ROOM-1"));
            std::unique_ptr<TcpConnection> server;
            bool replied = false;
            uint32_t now = 1000;
            for (int i = 0; i < 3000 && net.phase() != NetGame::Phase::Failed; ++i) {
                now += 10;
                net.update(now);
                if (!server) server = listener->accept();
                if (server && !replied) {
                    std::vector<uint8_t> m;
                    if (server->poll(m)) {
                        HelloMsg h;
                        ASSERT_TRUE(decode(m, h) && h.room == "ROOM-1" && h.version == kProtocolVersion && key_is_zero(h.key) && h.have_turns == 0);      // (a new player: no key)
                        server->send(encode(RejectMsg{c.reason}));
                        replied = true;
                    }
                }
                std::this_thread::sleep_for(std::chrono::microseconds(300));
            }
            ASSERT_TRUE(replied);
            ASSERT_EQ(net.phase(), NetGame::Phase::Failed);
            ASSERT_EQ(net.status_text(), c.text);
            bool failed = false;
            for (const NetGame::Event& e : net.take_events()) failed = failed || e.type == NetGame::Event::Type::Failed;
            ASSERT_TRUE(failed);
        }
    } TEST_END();
}

// ---- chat in the waiting room and the fill of a room on the local network (protocol 11) -----------------------------------------------------------

// The lines of a machine's room chat as "seat|name|text"
std::vector<std::string> room_lines(const std::vector<ChatLine>& lines) {
    std::vector<std::string> out;
    for (const ChatLine& l : lines) out.push_back(std::to_string(static_cast<unsigned>(l.seat)) + "|" + l.name + "|" + l.text);
    return out;
}

void run_room_chat_tests() {
    TEST_CASE("N3.18 Chat In The Waiting Room (Protocol 11) On A Room Of The Local Network: A Line Of The Host Or Of A Guest Reaches Everybody In Seat Order With Its Name (The Latest Of Somebody Else's Is On The Status Line For A Few Seconds, Never Its Own), Chat Works While The Map Loads And The Lines Are Kept When The Match Has Begun; Nothing Is Sent Out Of A Room; The Match's Chat Is As Before") {
        Table t;
        ASSERT_TRUE(make_room(t, 2));
        Machine& alice = *t.machines[0];
        Machine& bob = *t.machines[1];
        Machine& carol = *t.machines[2];
        ASSERT_TRUE(alice.net.chat("hello room"));
        ASSERT_TRUE(t.run_until([&]() { return bob.net.pregame_chat().size() == 1 && carol.net.pregame_chat().size() == 1; }, 3000));
        ASSERT_EQ(room_lines(bob.net.take_pregame_chat()), (std::vector<std::string>{"0|Alice|hello room"}));
        ASSERT_TRUE(bob.net.take_pregame_chat().empty());                                  // handed out once
        ASSERT_EQ(room_lines(carol.net.take_pregame_chat()), (std::vector<std::string>{"0|Alice|hello room"}));
        ASSERT_EQ(room_lines(alice.net.take_pregame_chat()), (std::vector<std::string>{"0|Alice|hello room"}));        // the host's own line is in its own log too
        ASSERT_TRUE(bob.saw(NetGame::Event::Type::Chat) && carol.saw(NetGame::Event::Type::Chat));
        ASSERT_EQ(bob.net.status_text(), std::string("Alice: hello room"));               // the status line shows somebody else's line ...
        ASSERT_EQ(alice.net.status_text(), std::string(sim::strings::text(sim::strings::kPressStart)));          // ... never one's own
        ASSERT_TRUE(bob.net.chat("hi Alice, Bob here"));
        ASSERT_TRUE(t.run_until([&]() { return alice.net.pregame_chat().size() == 2 && carol.net.pregame_chat().size() == 2 && bob.net.pregame_chat().size() == 2; }, 3000));
        ASSERT_EQ(room_lines(carol.net.pregame_chat()), (std::vector<std::string>{"0|Alice|hello room", "1|Bob|hi Alice, Bob here"}));
        ASSERT_EQ(room_lines(alice.net.pregame_chat()), room_lines(carol.net.pregame_chat()));
        ASSERT_EQ(room_lines(bob.net.pregame_chat()), room_lines(carol.net.pregame_chat()));
        ASSERT_EQ(carol.net.status_text(), std::string("Bob: hi Alice, Bob here"));
        ASSERT_EQ(alice.net.status_text(), std::string("Bob: hi Alice, Bob here"));
        t.run(6000);                                                                        // five seconds later the standing prompt is back (Bob's own line never showed)
        ASSERT_EQ(carol.net.status_text(), std::string(sim::strings::text(sim::strings::kWaitingForHost)));
        ASSERT_EQ(bob.net.status_text(), std::string(sim::strings::text(sim::strings::kWaitingForHost)));
        // a long line is cut to 100 characters, a line of nothing is not sent, the status line holds at most 84 characters of one
        ASSERT_TRUE(carol.net.chat(std::string(150, 'x')));
        ASSERT_FALSE(carol.net.chat(""));
        ASSERT_TRUE(t.run_until([&]() { return alice.net.pregame_chat().size() == 3; }, 3000));
        ASSERT_EQ(alice.net.pregame_chat().back().text.size(), kMaxChatChars);
        ASSERT_TRUE(bob.net.status_text().size() <= 84);
        // chat while the map loads: Carol loads but does not report yet (a slow disk), so the room stays in Loading
        carol.hold_load = true;
        uint64_t hash = 0;
        ASSERT_TRUE(hash_file(maps_dir() + "TINY.LVL", hash));
        ASSERT_TRUE(alice.net.start_match(7, hash));
        ASSERT_TRUE(t.run_until([&]() { return bob.net.phase() == NetGame::Phase::Loading && carol.net.phase() == NetGame::Phase::Loading && carol.loads == 1; }, 3000));
        t.run(300);
        ASSERT_EQ(alice.net.phase(), NetGame::Phase::Loading);                              // (still waiting for Carol)
        ASSERT_TRUE(bob.net.chat("loaded and waiting"));
        ASSERT_TRUE(t.run_until([&]() { return carol.net.pregame_chat().size() == 4 && alice.net.pregame_chat().size() == 4; }, 3000));
        ASSERT_EQ(carol.net.pregame_chat().back().text, std::string("loaded and waiting"));
        ASSERT_EQ(carol.net.pregame_chat().back().name, std::string("Bob"));
        carol.net.report_loaded(true);
        ASSERT_TRUE(t.run_until([&]() { return everybody_playing(t); }, 5000));
        // the lines are still there when the match has begun: the application decides what to do with them
        for (Machine* m : {&alice, &bob, &carol}) {
            ASSERT_EQ(m->net.pregame_chat().size(), size_t{4});
            ASSERT_EQ(m->net.pregame_chat().front().text, std::string("hello room"));
        }
        // and the chat of the match is the match's: the callback has it, with the team flag
        ASSERT_TRUE(alice.net.chat("in the match", false));
        ASSERT_TRUE(t.run_until([&]() { return !bob.chats.empty() && !carol.chats.empty(); }, 3000));
        ASSERT_TRUE(bob.chats.back().text == "in the match" && bob.chats.back().sender == 0 && !bob.chats.back().team);
        ASSERT_EQ(bob.net.pregame_chat().size(), size_t{4});                                // (the match's chat is not the room's)
        // nothing is sent out of a room: a machine that is off, one that has not joined, empty lines
        sim::SimulationEngine off_sim;
        NetGame off(off_sim);
        ASSERT_FALSE(off.chat("anybody"));
        ASSERT_TRUE(off.pregame_chat().empty() && off.take_pregame_chat().empty());
    } TEST_END();

    TEST_CASE("N3.19 The Fill Of A Room On The Local Network (Protocol 11): The Host's Own START Can Seat A Bot In Every Empty Seat (Named \"Bot (Level)\", Good Thumbs): Alone It Takes Three, With Two Guests One; The Start Then Needs No Other Player; Fog Of War Refuses It (A Notice On The Status Line, Nothing Seated); A Cancelled Start's Bots Are Taken Out Again; Only The Host Of An Open Room Can Fill; None Fills Nothing") {
        {   // alone: three bots, and the host can start although nobody else is there
            Table t;
            Machine& host = t.add("Alice");
            ASSERT_TRUE(host.net.host(0, "Alice", true));
            host.net.set_map("TINY.LVL");
            t.run(200);
            ASSERT_FALSE(host.net.can_start());
            ASSERT_EQ(host.net.fill_bots(FillLevel::None), size_t{0});                      // none fills nothing
            ASSERT_FALSE(host.net.can_start());
            std::vector<uint8_t> seats;
            ASSERT_EQ(host.net.fill_bots(FillLevel::Medium, &seats), size_t{3});
            ASSERT_EQ(seats, (std::vector<uint8_t>{1, 2, 3}));
            for (const uint8_t seat : seats) {
                ASSERT_TRUE(host.net.room().slots[seat].state == SlotState::Bot && host.net.room().slots[seat].name == "Bot (Medium)");
                ASSERT_EQ(host.net.seat_quality(seat), LinkQuality::Good);                  // a bot's thumb is good
            }
            ASSERT_TRUE(host.net.can_start());                                              // four players, one of them a person
            ASSERT_EQ(host.net.fill_bots(FillLevel::Hard), size_t{0});                      // nothing left to fill
            uint64_t hash = 0;
            ASSERT_TRUE(hash_file(maps_dir() + "TINY.LVL", hash));
            ASSERT_TRUE(host.net.start_match(5, hash));
            ASSERT_EQ(host.net.start_info().roster, 0x0F);
            ASSERT_TRUE(host.net.start_info().names[1] == "Bot (Medium)" && host.net.start_info().names[3] == "Bot (Medium)");
            ASSERT_TRUE(t.run_until([&]() { return host.net.phase() == NetGame::Phase::Playing; }, 3000));
            ASSERT_FALSE(host.net.add_bot(2, "late"));
            ASSERT_EQ(host.net.fill_bots(FillLevel::Easy), size_t{0});                      // (a match that runs is no room)
        }
        {   // with two guests: one bot, in the seat that is left; a fill never takes a seat that a person holds
            Table t;
            ASSERT_TRUE(make_room(t, 2));
            Machine& host = *t.machines[0];
            std::vector<uint8_t> seats;
            ASSERT_EQ(host.net.fill_bots(FillLevel::Easy, &seats), size_t{1});
            ASSERT_EQ(seats, (std::vector<uint8_t>{3}));
            ASSERT_TRUE(host.net.room().slots[3].state == SlotState::Bot && host.net.room().slots[3].name == "Bot (Easy)");
            ASSERT_TRUE(t.run_until([&]() { return t.machines[1]->net.room().slots[3].state == SlotState::Bot && t.machines[2]->net.room().slots[3].name == "Bot (Easy)"; }, 3000));      // the guests see the bot
            // a guest cannot fill, and a fill leaves the room as it is for the people
            ASSERT_EQ(t.machines[1]->net.fill_bots(FillLevel::Hard), size_t{0});
            ASSERT_TRUE(t.machines[1]->net.room().slots[1].state == SlotState::Client && t.machines[1]->net.room().slots[3].name == "Bot (Easy)");
            // the start is cancelled (Carol cannot load the map): the bots go again and the room is as it was
            t.machines[2]->corrupt_map = true;
            uint64_t hash = 0;
            ASSERT_TRUE(hash_file(maps_dir() + "TINY.LVL", hash));
            ASSERT_TRUE(host.net.start_match(9, hash));
            ASSERT_TRUE(t.run_until([&]() { return host.net.phase() == NetGame::Phase::Room && host.saw(NetGame::Event::Type::Cancelled); }, 5000));
            host.net.remove_bot(3);                                                         // (what the application does on the Cancelled event)
            ASSERT_TRUE(host.net.room().slots[3].state == SlotState::Empty);
            ASSERT_TRUE(t.run_until([&]() { return t.machines[1]->net.room().slots[3].state == SlotState::Empty; }, 3000));
        }
        {   // Fog of War: bots would see through it
            Table t;
            Machine& host = t.add("Alice");
            ASSERT_TRUE(host.net.host(0, "Alice", true));
            host.net.set_map("TINY.LVL");
            host.net.set_fog(true);
            t.run(200);
            ASSERT_EQ(host.net.fill_bots(FillLevel::Medium), size_t{0});
            ASSERT_EQ(host.net.status_text(), std::string(kNoticeFillFog));
            ASSERT_FALSE(host.net.can_start());
            for (uint8_t seat = 1; seat < 4; ++seat) ASSERT_TRUE(host.net.room().slots[seat].state == SlotState::Empty);
        }
        {   // a guest that has not been welcomed, a machine that is off: nothing to fill
            sim::SimulationEngine sim;
            NetGame off(sim);
            ASSERT_EQ(off.fill_bots(FillLevel::Medium), size_t{0});
            off.set_fill_bots(FillLevel::Hard);
            ASSERT_EQ(off.fill_bots(), FillLevel::Hard);
            ASSERT_FALSE(off.request_start());                                               // not a leader of anything
        }
    } TEST_END();
}

}  // namespace

int main() {
    std::cout << "\n=======================================================\n [SUITE] Network port: NetGame (room, start barrier, match) over real sockets\n"
                 "=======================================================\n";
    run_room_tests();
    run_thumb_tests();
    run_start_tests();
    run_match_tests();
    run_lan_tests();
    run_seat_tests();
    run_migration_tests();
    run_reject_tests();
    run_room_chat_tests();
    std::cout << "\n=======================================================\n Total Test Cases: " << g_test_count << "\n Total Assertions: " << g_assert_count
              << "\n Failed:           " << g_test_failures << "\n=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}
