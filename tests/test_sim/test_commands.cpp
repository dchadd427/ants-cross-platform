// Tests of the command layer and the state hash of the network port (docs/GAME_REVERSE_ENGINEERING.md, "Network port"): a Command is the
// intent of one player as plain data; SimulationEngine::apply_command validates it (issuer, ownership, ranges) and applies it; two engines that
// apply the same commands at the same ticks in canonical order stay bit-identical, which state_hash() proves every tick.
#include "ants_assets/lvl_parser.hpp"
#include "ants_sim/command.hpp"
#include "ants_sim/sim_engine.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

using namespace ants::sim;

#ifndef ORIGINAL_ASSETS_DIR
#define ORIGINAL_ASSETS_DIR "Original-Ants"
#endif

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
    ++g_test_count;
    std::cout << "  RUNNING: " << std::left << std::setw(96) << name << " ... " << std::flush;
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
#define ASSERT_NE(a, b) ASSERT_TRUE((a) != (b))

namespace {

using Status = CommandResult::Status;

// A tiny deterministic generator for the scripts and the fuzzing (the tests must not depend on the library's PRNG)
struct Lcg {
    uint32_t s;
    explicit Lcg(uint32_t seed) : s(seed) {}
    uint32_t next() {
        s = s * 1664525u + 1013904223u;
        return s >> 8;
    }
    uint32_t below(uint32_t n) { return next() % n; }
};

Command make_command(CommandType type, uint8_t issuer, uint8_t other = 255, int16_t x = 0, int16_t y = 0, std::vector<uint32_t> ants = {}) {
    Command c;
    c.type = type;
    c.issuer = issuer;
    c.other_player = other;
    c.tile_x = x;
    c.tile_y = y;
    c.ants = std::move(ants);
    return c;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Scenario: four players on a 60 x 60 map with every ant type, a river, a lunchbox and bombs
// ---------------------------------------------------------------------------------------------------------------------------------

struct World {
    SimulationEngine sim;
    std::vector<uint32_t> ants[MAX_PLAYERS];
};

void build_world(World& w, uint32_t seed) {
    SimulationEngine& sim = w.sim;
    sim.init_test_world(60, 60, seed, 720000);
    const TileCoord hills[MAX_PLAYERS] = {{4, 4}, {50, 4}, {4, 50}, {50, 50}};
    for (uint8_t p = 0; p < MAX_PLAYERS; ++p) sim.grid_mut().set_anthill(p, hills[p]);
    for (int32_t y = 0; y < 60; ++y) sim.grid_mut().set_terrain(30, y, TERRAIN_WATER);      // a river down the middle
    for (int step = 0; step < 4; ++step) sim.grid_mut().advance_bridge(30, 30, 0);           // with one bridge
    sim.grid_mut().drop_lunchbox(20, 20, 25);
    sim.grid_mut().place_bomb(25, 25, 1);
    const AntType types[6] = {AntType::Worker, AntType::Bomber, AntType::Fire, AntType::Thief, AntType::Combat, AntType::Swimmer};
    for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
        for (int i = 0; i < 6; ++i) {
            const int32_t x = hills[p].x + 1 + i;
            const int32_t y = hills[p].y + 6;
            w.ants[p].push_back(sim.spawn_unit(p, types[i], TileCoord{x, y}));
        }
    }
}

// The commands of tick `t`: every 8 ticks each player does something random with its own ants (deterministic)
std::vector<Command> script_for_tick(const World& w, uint32_t seed, uint32_t t) {
    std::vector<Command> out;
    for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
        Lcg rng(seed * 7919u + t * 104729u + p * 31u);
        if (t % 8 != p) continue;
        const uint32_t kind = rng.below(10);
        const auto& mine = w.ants[p];
        std::vector<uint32_t> pick;
        for (uint32_t id : mine) {
            if (rng.below(3) != 0) pick.push_back(id);
        }
        if (pick.empty()) pick.push_back(mine[0]);
        const int16_t x = static_cast<int16_t>(rng.below(60));
        const int16_t y = static_cast<int16_t>(rng.below(60));
        if (kind < 4) out.push_back(make_command(CommandType::GroupMove, p, 255, x, y, pick));
        else if (kind < 6) out.push_back(make_command(CommandType::GroupAttack, p, 255, x, y, pick));
        else if (kind < 8) out.push_back(make_command(CommandType::GroupSpecial, p, 255, x, y, pick));
        else if (kind == 8) out.push_back(make_command(CommandType::Stop, p, 255, 0, 0, pick));
        else out.push_back(make_command(CommandType::Hatch, p));
    }
    return out;
}

// Merges the per-issuer queues in a random interleaving: the arrival order at the sequencer (each issuer's own order is kept)
std::vector<Command> permuted_arrival(const std::vector<Command>& in, Lcg& rng) {
    std::vector<std::vector<Command>> q(MAX_PLAYERS);
    for (const Command& c : in) q[c.issuer].push_back(c);
    std::vector<Command> out;
    std::vector<size_t> pos(MAX_PLAYERS, 0);
    size_t left = in.size();
    while (left > 0) {
        const uint32_t p = rng.below(MAX_PLAYERS);
        if (pos[p] >= q[p].size()) continue;
        out.push_back(q[p][pos[p]++]);
        --left;
    }
    return out;
}

// ---------------------------------------------------------------------------------------------------------------------------------

void run_codec_tests() {
    TEST_CASE("N1.1 Command Codec: Every Type Round-Trips Byte For Byte And Reports What It Consumed") {
        Lcg rng(42);
        for (int i = 0; i < 2000; ++i) {
            Command c;
            c.type = static_cast<CommandType>(1 + rng.below(10));
            c.issuer = static_cast<uint8_t>(rng.below(256));
            c.other_player = static_cast<uint8_t>(rng.below(256));
            c.tile_x = static_cast<int16_t>(static_cast<int32_t>(rng.below(65536)) - 32768);
            c.tile_y = static_cast<int16_t>(static_cast<int32_t>(rng.below(65536)) - 32768);
            if (has_ant_list(c.type)) {
                const uint32_t n = 1 + rng.below(kMaxCommandAnts);
                for (uint32_t k = 0; k < n; ++k) c.ants.push_back(rng.next() * 257u + rng.next());
            }
            std::vector<uint8_t> bytes;
            encode(c, bytes);
            ASSERT_EQ(bytes.size(), encoded_size(c));
            std::vector<uint8_t> padded = bytes;
            padded.push_back(0xAB);                                    // trailing data of the next command is left alone
            Command d;
            size_t used = 0;
            ASSERT_EQ(decode(padded.data(), padded.size(), d, &used), DecodeError::None);
            ASSERT_EQ(used, bytes.size());
            ASSERT_TRUE(c == d);
        }
    } TEST_END();

    TEST_CASE("N1.2 Command Codec: Malformed Input Is Rejected Before It Is Used") {
        Command out;
        ASSERT_EQ(decode(nullptr, 0, out), DecodeError::Truncated);
        std::vector<uint8_t> b;
        encode(make_command(CommandType::GroupMove, 1, 255, 5, 6, {1, 2, 3}), b);
        for (size_t cut = 0; cut < b.size(); ++cut) {                 // every prefix of a valid command is truncated
            ASSERT_EQ(decode(b.data(), cut, out), DecodeError::Truncated);
        }
        std::vector<uint8_t> bad = b;
        bad[0] = 0;                                                   // no command
        ASSERT_EQ(decode(bad.data(), bad.size(), out), DecodeError::UnknownType);
        bad[0] = static_cast<uint8_t>(CommandType::Last) + 1;
        ASSERT_EQ(decode(bad.data(), bad.size(), out), DecodeError::UnknownType);
        bad[0] = 255;
        ASSERT_EQ(decode(bad.data(), bad.size(), out), DecodeError::UnknownType);
        bad = b;
        bad[7] = static_cast<uint8_t>(kMaxCommandAnts + 1);           // more ants than a command may name
        bad.resize(kCommandHeaderBytes + 4 * (kMaxCommandAnts + 1), 0);
        ASSERT_EQ(decode(bad.data(), bad.size(), out), DecodeError::BadCount);
        bad = b;
        bad[7] = 255;
        ASSERT_EQ(decode(bad.data(), bad.size(), out), DecodeError::BadCount);
        bad = b;
        bad[7] = 0;                                                   // a group order without ants
        ASSERT_EQ(decode(bad.data(), bad.size(), out), DecodeError::BadCount);
        std::vector<uint8_t> hatch;
        encode(make_command(CommandType::Hatch, 2), hatch);
        hatch[7] = 1;                                                 // an ant list on a command that has none
        hatch.resize(kCommandHeaderBytes + 4, 0);
        ASSERT_EQ(decode(hatch.data(), hatch.size(), out), DecodeError::BadCount);
    } TEST_END();

    TEST_CASE("N1.3 Command Codec: 300000 Random And Mutated Buffers Never Crash And Only Give Consistent Commands") {
        Lcg rng(7);
        std::vector<uint8_t> valid;
        encode(make_command(CommandType::GroupAttack, 3, 255, 12, 13, {4, 5, 6, 7}), valid);
        size_t decoded = 0;
        for (int i = 0; i < 300000; ++i) {
            std::vector<uint8_t> buf;
            if (i % 2 == 0) {
                buf.resize(rng.below(64));
                for (auto& x : buf) x = static_cast<uint8_t>(rng.below(256));
            } else {
                buf = valid;
                for (uint32_t m = 1 + rng.below(3); m > 0; --m) buf[rng.below(static_cast<uint32_t>(buf.size()))] = static_cast<uint8_t>(rng.below(256));
                if (rng.below(4) == 0) buf.resize(rng.below(static_cast<uint32_t>(buf.size()) + 1));
            }
            Command c;
            size_t used = 0;
            if (decode(buf.data(), buf.size(), c, &used) != DecodeError::None) continue;
            ++decoded;
            ASSERT_TRUE(used <= buf.size());
            ASSERT_TRUE(c.type != CommandType::None && c.type <= CommandType::Last);
            ASSERT_TRUE(c.ants.size() <= kMaxCommandAnts);
            ASSERT_TRUE(has_ant_list(c.type) ? !c.ants.empty() : c.ants.empty());
            std::vector<uint8_t> again;
            encode(c, again);
            ASSERT_TRUE(again.size() == used && std::equal(again.begin(), again.end(), buf.begin()));
        }
        ASSERT_TRUE(decoded > 1000);                                  // the mutated valid commands do get through
    } TEST_END();

    TEST_CASE("N1.4 Canonical Order: By Issuer, Each Issuer's Commands Keep Their Submission Order") {
        std::vector<Command> v = {make_command(CommandType::GroupMove, 2, 255, 1, 1, {1}), make_command(CommandType::Stop, 0, 255, 0, 0, {2}),
                                  make_command(CommandType::Hatch, 1), make_command(CommandType::GroupMove, 0, 255, 9, 9, {3}),
                                  make_command(CommandType::Hatch, 2), make_command(CommandType::GroupAttack, 0, 255, 5, 5, {4})};
        canonical_order(v);
        ASSERT_EQ(v[0].issuer, 0);
        ASSERT_EQ(v[1].issuer, 0);
        ASSERT_EQ(v[2].issuer, 0);
        ASSERT_EQ(v[0].type, CommandType::Stop);
        ASSERT_EQ(v[1].type, CommandType::GroupMove);
        ASSERT_EQ(v[2].type, CommandType::GroupAttack);
        ASSERT_EQ(v[3].issuer, 1);
        ASSERT_EQ(v[4].issuer, 2);
        ASSERT_EQ(v[4].type, CommandType::GroupMove);
        ASSERT_EQ(v[5].type, CommandType::Hatch);
    } TEST_END();
}

void run_validation_tests() {
    TEST_CASE("N1.5 apply_command: The Issuer Must Be A Player; Nothing Happens After The End Of The Match") {
        World w;
        build_world(w, 1);
        ASSERT_EQ(w.sim.apply_command(make_command(CommandType::GroupMove, 4, 255, 10, 10, {w.ants[0][0]})).status, Status::RejectedIssuer);
        ASSERT_EQ(w.sim.apply_command(make_command(CommandType::GroupMove, 255, 255, 10, 10, {w.ants[0][0]})).status, Status::RejectedIssuer);
        ASSERT_EQ(w.sim.apply_command(make_command(CommandType::None, 0)).status, Status::RejectedMalformed);
        ASSERT_EQ(w.sim.apply_command(make_command(static_cast<CommandType>(99), 0)).status, Status::RejectedMalformed);
        w.sim.set_match_time_remaining_ms(0);
        for (int t = 0; t < 8; ++t) w.sim.tick();
        ASSERT_TRUE(w.sim.is_match_over());
        ASSERT_EQ(w.sim.apply_command(make_command(CommandType::GroupMove, 0, 255, 10, 10, {w.ants[0][0]})).status, Status::Ignored);
    } TEST_END();

    TEST_CASE("N1.6 apply_command: A Group Order Only Touches The Issuer's Own Ants (foreign, unknown and repeated ids are dropped)") {
        World w;
        build_world(w, 1);
        const uint32_t mine = w.ants[0][0];
        const uint32_t theirs = w.ants[1][0];
        // player 0 names its own ant, an enemy ant, an unknown id and its own ant again
        CommandResult r = w.sim.apply_command(make_command(CommandType::GroupMove, 0, 255, 20, 30, {mine, theirs, 99999, mine}));
        ASSERT_EQ(r.status, Status::Applied);
        ASSERT_EQ(r.ants_ordered, 1u);
        ASSERT_EQ(w.sim.get_unit(mine).orig_order, AntUnit::kOrderMove);
        ASSERT_EQ(w.sim.get_unit(mine).orig_order_tile, (TileCoord{20, 30}));
        ASSERT_EQ(w.sim.get_unit(theirs).orig_order, AntUnit::kOrderNone);
        ASSERT_EQ(r.ack_ant, mine);
        // an order that names only foreign ants is valid but does nothing
        r = w.sim.apply_command(make_command(CommandType::GroupMove, 2, 255, 5, 5, {theirs}));
        ASSERT_EQ(r.status, Status::Ignored);
        ASSERT_EQ(w.sim.get_unit(theirs).orig_order, AntUnit::kOrderNone);
        // malformed: no ants, too many, a tile outside the map
        ASSERT_EQ(w.sim.apply_command(make_command(CommandType::GroupAttack, 0, 255, 5, 5, {})).status, Status::RejectedMalformed);
        std::vector<uint32_t> many(kMaxCommandAnts + 1, mine);
        ASSERT_EQ(w.sim.apply_command(make_command(CommandType::GroupMove, 0, 255, 5, 5, many)).status, Status::RejectedMalformed);
        const std::vector<std::pair<int16_t, int16_t>> off_map = {{-1, 5}, {5, -1}, {60, 5}, {5, 60}, {32767, 0}, {0, -32768}};
        for (const auto& xy : off_map) {
            ASSERT_EQ(w.sim.apply_command(make_command(CommandType::GroupSpecial, 0, 255, xy.first, xy.second, {mine})).status, Status::RejectedMalformed);
        }
        // the three kinds: special reaches the ability classification, attack targets the enemy that stands there
        r = w.sim.apply_command(make_command(CommandType::GroupSpecial, 0, 255, 25, 25, {w.ants[0][1]}));         // the bomber, onto a bomb
        ASSERT_EQ(r.status, Status::Applied);
        ASSERT_EQ(w.sim.get_unit(w.ants[0][1]).orig_order, AntUnit::kOrderDefuse);
    } TEST_END();

    TEST_CASE("N1.7 apply_command: Stop Reaches Only The Issuer's Ants") {
        World w;
        build_world(w, 1);
        const uint32_t mine = w.ants[0][0];
        const uint32_t theirs = w.ants[1][0];
        w.sim.apply_command(make_command(CommandType::GroupMove, 0, 255, 20, 30, {mine}));
        w.sim.apply_command(make_command(CommandType::GroupMove, 1, 255, 40, 30, {theirs}));
        for (int t = 0; t < 10; ++t) w.sim.tick();
        ASSERT_EQ(w.sim.get_unit(mine).orig_order, AntUnit::kOrderMove);
        ASSERT_EQ(w.sim.get_unit(theirs).orig_order, AntUnit::kOrderMove);
        // player 1 tries to stop player 0's ant: nothing happens to it
        CommandResult r = w.sim.apply_command(make_command(CommandType::Stop, 1, 255, 0, 0, {mine}));
        ASSERT_EQ(r.status, Status::Ignored);
        ASSERT_EQ(w.sim.get_unit(mine).orig_order_tile, (TileCoord{20, 30}));
        // player 0 stops it: it is sent to its own tile
        r = w.sim.apply_command(make_command(CommandType::Stop, 0, 255, 0, 0, {mine, theirs}));
        ASSERT_EQ(r.status, Status::Applied);
        ASSERT_EQ(r.ants_ordered, 1u);
        ASSERT_NE(w.sim.get_unit(mine).orig_order_tile, (TileCoord{20, 30}));
        ASSERT_EQ(w.sim.get_unit(theirs).orig_order_tile, (TileCoord{40, 30}));
        ASSERT_EQ(w.sim.apply_command(make_command(CommandType::Stop, 0, 255, 0, 0, {})).status, Status::RejectedMalformed);
    } TEST_END();

    TEST_CASE("N1.8 apply_command: Hatch Acts For The Issuer Only, With The Original's Refusals") {
        World w;
        build_world(w, 1);
        w.sim.set_player_score(0, 0);
        CommandResult r = w.sim.apply_command(make_command(CommandType::Hatch, 0));
        ASSERT_EQ(r.status, Status::Applied);
        ASSERT_EQ(r.hatch_result, static_cast<uint8_t>(SimulationEngine::HatchResult::NotEnoughPoints));
        w.sim.set_player_score(0, 500);
        r = w.sim.apply_command(make_command(CommandType::Hatch, 0));
        ASSERT_EQ(r.hatch_result, static_cast<uint8_t>(SimulationEngine::HatchResult::Started));
        ASSERT_EQ(w.sim.get_pending_hatch_count(0), 1u);
        ASSERT_EQ(w.sim.get_pending_hatch_count(1), 0u);
        ASSERT_EQ(w.sim.get_player_score(0), 300);
        r = w.sim.apply_command(make_command(CommandType::Hatch, 0));
        ASSERT_EQ(r.hatch_result, static_cast<uint8_t>(SimulationEngine::HatchResult::AlreadyHatching));
    } TEST_END();

    TEST_CASE("N1.9 apply_command: The Alliance Protocol As Commands (invite, answer needs the invitation, withdraw, break)") {
        World w;
        build_world(w, 1);
        ASSERT_EQ(w.sim.apply_command(make_command(CommandType::AllianceInvite, 0, 0)).status, Status::RejectedMalformed);     // not with oneself
        ASSERT_EQ(w.sim.apply_command(make_command(CommandType::AllianceInvite, 0, 4)).status, Status::RejectedMalformed);
        ASSERT_EQ(w.sim.apply_command(make_command(CommandType::AllianceInvite, 0, 255)).status, Status::RejectedMalformed);
        // answering an invitation nobody made
        ASSERT_EQ(w.sim.apply_command(make_command(CommandType::AllianceAccept, 1, 0)).status, Status::RejectedNotAllowed);
        ASSERT_EQ(w.sim.apply_command(make_command(CommandType::AllianceDeny, 1, 0)).status, Status::RejectedNotAllowed);
        ASSERT_EQ(w.sim.get_ally_id(0), ALLIANCE_NONE);
        // player 0 invites player 1; player 2 cannot answer for it, nor can player 1 answer the wrong proposer
        ASSERT_EQ(w.sim.apply_command(make_command(CommandType::AllianceInvite, 0, 1)).status, Status::Applied);
        ASSERT_EQ(w.sim.apply_command(make_command(CommandType::AllianceAccept, 2, 0)).status, Status::RejectedNotAllowed);
        ASSERT_EQ(w.sim.apply_command(make_command(CommandType::AllianceAccept, 1, 3)).status, Status::RejectedNotAllowed);
        for (int t = 0; t < 100; ++t) w.sim.tick();                                                                          // no bot accepts it
        ASSERT_EQ(w.sim.get_ally_id(0), ALLIANCE_NONE);
        ASSERT_EQ(w.sim.apply_command(make_command(CommandType::AllianceAccept, 1, 0)).status, Status::Applied);
        ASSERT_EQ(w.sim.get_ally_id(0), 1);
        ASSERT_EQ(w.sim.get_ally_id(1), 0);
        // the accepted invitation is used up
        ASSERT_EQ(w.sim.apply_command(make_command(CommandType::AllianceAccept, 1, 0)).status, Status::RejectedNotAllowed);
        // a refusal
        ASSERT_EQ(w.sim.apply_command(make_command(CommandType::AllianceInvite, 2, 3)).status, Status::Applied);
        ASSERT_EQ(w.sim.apply_command(make_command(CommandType::AllianceDeny, 3, 2)).status, Status::Applied);
        ASSERT_EQ(w.sim.get_ally_id(2), ALLIANCE_NONE);
        // a withdrawn invitation cannot be accepted
        ASSERT_EQ(w.sim.apply_command(make_command(CommandType::AllianceInvite, 2, 3)).status, Status::Applied);
        ASSERT_EQ(w.sim.apply_command(make_command(CommandType::AllianceWithdraw, 2, 3)).status, Status::Applied);
        ASSERT_EQ(w.sim.apply_command(make_command(CommandType::AllianceAccept, 3, 2)).status, Status::RejectedNotAllowed);
        // breaking one's team
        ASSERT_EQ(w.sim.apply_command(make_command(CommandType::AllianceBreak, 1)).status, Status::Applied);
        ASSERT_EQ(w.sim.get_ally_id(0), ALLIANCE_NONE);
        ASSERT_EQ(w.sim.get_ally_id(1), ALLIANCE_NONE);
    } TEST_END();
}

// ---------------------------------------------------------------------------------------------------------------------------------

// Runs one scripted match on two engines that receive the same commands with different arrival orders and compares the hashes every tick
bool lockstep_run(uint32_t seed, uint32_t ticks, std::string& why, size_t* applied_out = nullptr) {
    World a;
    World b;
    build_world(a, seed);
    build_world(b, seed);
    if (a.sim.state_hash() != b.sim.state_hash()) {
        why = "worlds differ before the first tick";
        return false;
    }
    Lcg arrival(seed ^ 0xABCDEFu);
    size_t applied = 0;
    StateHash last = a.sim.state_hash();
    bool changed = false;
    for (uint32_t t = 0; t < ticks; ++t) {
        std::vector<Command> turn = script_for_tick(a, seed, t);
        std::vector<Command> for_a = permuted_arrival(turn, arrival);
        std::vector<Command> for_b = permuted_arrival(turn, arrival);
        canonical_order(for_a);
        canonical_order(for_b);
        for (const Command& c : for_a) {
            if (a.sim.apply_command(c).accepted()) ++applied;
        }
        for (const Command& c : for_b) b.sim.apply_command(c);
        a.sim.tick();
        b.sim.tick();
        const StateHash ha = a.sim.state_hash();
        const StateHash hb = b.sim.state_hash();
        if (ha != hb) {
            why = "hashes differ after tick " + std::to_string(t + 1) + " (engine " + std::to_string(ha.engine == hb.engine) + " players " +
                  std::to_string(ha.players == hb.players) + " grid " + std::to_string(ha.grid == hb.grid) + " food " + std::to_string(ha.food == hb.food) +
                  " ants " + std::to_string(ha.ants == hb.ants) + " paths " + std::to_string(ha.paths == hb.paths) + ")";
            return false;
        }
        if (ha != last) changed = true;
        last = ha;
    }
    if (!changed) {
        why = "the state never changed";
        return false;
    }
    if (applied_out != nullptr) *applied_out = applied;
    return true;
}

void run_lockstep_tests() {
    TEST_CASE("N1.10 Lock-Step: Two Engines With Permuted Command Arrival Stay Bit-Identical Every Tick (4 players, 600 ticks, 3 seeds)") {
        for (uint32_t seed : {1u, 2u, 3u}) {
            std::string why;
            size_t applied = 0;
            const bool ok = lockstep_run(seed, 600, why, &applied);
            if (!ok) std::cout << "\n    seed " << seed << ": " << why << "\n";
            ASSERT_TRUE(ok);
            ASSERT_TRUE(applied > 50);                                // the script really did things
        }
    } TEST_END();

    TEST_CASE("N1.11 State Hash: Different Seeds, Different Commands And Different Ticks Give Different Hashes; Equal Runs Give Equal Ones") {
        World a;
        World b;
        World c;
        build_world(a, 1);
        build_world(b, 1);
        build_world(c, 2);
        ASSERT_TRUE(a.sim.state_hash() == b.sim.state_hash());
        ASSERT_TRUE(a.sim.state_hash() != c.sim.state_hash());                    // the seed is part of the state
        a.sim.tick();
        ASSERT_TRUE(a.sim.state_hash() != b.sim.state_hash());                    // a tick is
        b.sim.tick();
        ASSERT_TRUE(a.sim.state_hash() == b.sim.state_hash());
        a.sim.apply_command(make_command(CommandType::GroupMove, 0, 255, 20, 30, {a.ants[0][0]}));
        ASSERT_TRUE(a.sim.state_hash() != b.sim.state_hash());                    // an order is
        b.sim.apply_command(make_command(CommandType::GroupMove, 0, 255, 20, 31, {b.ants[0][0]}));
        ASSERT_TRUE(a.sim.state_hash() != b.sim.state_hash());                    // a different tile is
        b.sim.apply_command(make_command(CommandType::GroupMove, 0, 255, 20, 30, {b.ants[0][0]}));
        for (int t = 0; t < 40; ++t) {
            a.sim.tick();
            b.sim.tick();
        }
        // b was ordered to a different tile first and then to the same one: the path manager queue, serials and clocks differ from a's history
        // only where they are state; the hash must equal iff the states are equal, so all we can say is that both are deterministic
        World a2;
        build_world(a2, 1);
        a2.sim.tick();
        a2.sim.apply_command(make_command(CommandType::GroupMove, 0, 255, 20, 30, {a2.ants[0][0]}));
        for (int t = 0; t < 40; ++t) a2.sim.tick();
        ASSERT_TRUE(a.sim.state_hash() == a2.sim.state_hash());                  // same history, same hash
    } TEST_END();
}

// ---------------------------------------------------------------------------------------------------------------------------------

struct FieldCase {
    const char* name;
    std::function<void(AntUnit&)> change;
};

void run_coverage_tests() {
    TEST_CASE("N1.12 State Hash Covers Every Field Of An Ant") {
        World w;
        build_world(w, 1);
        for (int t = 0; t < 5; ++t) w.sim.tick();
        const uint32_t id = w.ants[0][0];
        const std::vector<FieldCase> cases = {
            {"id", [](AntUnit& a) { a.id += 1000; }},
            {"player_id", [](AntUnit& a) { a.player_id ^= 1; }},
            {"team", [](AntUnit& a) { a.team = TeamId::Red; }},
            {"target_team_id", [](AntUnit& a) { a.target_team_id ^= 1; }},
            {"type", [](AntUnit& a) { a.type = AntType::Swimmer; }},
            {"state", [](AntUnit& a) { a.state = UnitState::Stunned; }},
            {"death_status", [](AntUnit& a) { a.death_status = DeathStatus::Drowned; }},
            {"hp", [](AntUnit& a) { a.hp = static_cast<uint16_t>(a.hp + 1); }},
            {"max_hp", [](AntUnit& a) { a.max_hp = static_cast<uint16_t>(a.max_hp + 1); }},
            {"pos", [](AntUnit& a) { a.pos.x += 1; }},
            {"pixel_x", [](AntUnit& a) { a.pixel_x += 1; }},
            {"pixel_y", [](AntUnit& a) { a.pixel_y += 1; }},
            {"facing", [](AntUnit& a) { a.facing = (a.facing == ants::assets::Direction::North) ? ants::assets::Direction::South : ants::assets::Direction::North; }},
            {"fx_x", [](AntUnit& a) { a.fx_x += 1; }},
            {"fx_y", [](AntUnit& a) { a.fx_y += 1; }},
            {"holding", [](AntUnit& a) { a.holding ^= 1; }},
            {"carried_food", [](AntUnit& a) { a.carried_food += 1; }},
            {"carried_points", [](AntUnit& a) { a.carried_points += 1; }},
            {"anim_subitem", [](AntUnit& a) { a.anim_subitem += 1; }},
            {"anim_tick", [](AntUnit& a) { a.anim_tick += 1; }},
            {"is_on_mud", [](AntUnit& a) { a.is_on_mud = !a.is_on_mud; }},
            {"was_in_water", [](AntUnit& a) { a.was_in_water = !a.was_in_water; }},
            {"in_water", [](AntUnit& a) { a.in_water = !a.in_water; }},
            {"waypoints", [](AntUnit& a) { a.waypoints.push_back(TileCoord{1, 2}); }},
            {"current_waypoint_idx", [](AntUnit& a) { a.current_waypoint_idx += 1; }},
            {"final_dest", [](AntUnit& a) { a.final_dest.x += 1; }},
            {"harvest_origin", [](AntUnit& a) { a.harvest_origin.x += 1; }},
            {"is_thief_steal", [](AntUnit& a) { a.is_thief_steal = !a.is_thief_steal; }},
            {"ability_target", [](AntUnit& a) { a.ability_target.x += 1; }},
            {"allow_friendly_bomb", [](AntUnit& a) { a.allow_friendly_bomb = !a.allow_friendly_bomb; }},
            {"engaged", [](AntUnit& a) { a.engaged = !a.engaged; }},
            {"frozen", [](AntUnit& a) { a.frozen = !a.frozen; }},
            {"pending_victim", [](AntUnit& a) { a.pending_victim += 1; }},
            {"pending_range", [](AntUnit& a) { a.pending_range += 1; }},
            {"pending_dir", [](AntUnit& a) { a.pending_dir += 1; }},
            {"pending_tile", [](AntUnit& a) { a.pending_tile.x += 1; }},
            {"killer_team", [](AntUnit& a) { a.killer_team ^= 1; }},
            {"last_order_ms", [](AntUnit& a) { a.last_order_ms += 1; }},
            {"knock_flag", [](AntUnit& a) { a.knock_flag = !a.knock_flag; }},
            {"flight_tile", [](AntUnit& a) { a.flight_tile.x += 1; }},
            {"removed", [](AntUnit& a) { a.removed = !a.removed; }},
            {"burn_end_ms", [](AntUnit& a) { a.burn_end_ms += 1; }},
            {"auto_engage", [](AntUnit& a) { a.auto_engage = !a.auto_engage; }},
            {"ae_order", [](AntUnit& a) { a.ae_order += 1; }},
            {"ae_target", [](AntUnit& a) { a.ae_target.x += 1; }},
            {"ae_home_state", [](AntUnit& a) { a.ae_home_state += 1; }},
            {"combevt_due_ms", [](AntUnit& a) { a.combevt_due_ms += 1; }},
            {"loco.clip.chd_index", [](AntUnit& a) { a.loco.clip.chd_index += 1; }},
            {"loco.clip.count", [](AntUnit& a) { a.loco.clip.count += 1; }},
            {"loco.clip.mirrored", [](AntUnit& a) { a.loco.clip.mirrored = !a.loco.clip.mirrored; }},
            {"loco.clip.flags", [](AntUnit& a) { a.loco.clip.flags ^= 1; }},
            {"loco.cursor", [](AntUnit& a) { a.loco.cursor += 1; }},
            {"loco.next_ms", [](AntUnit& a) { a.loco.next_ms += 1; }},
            {"loco.dir", [](AntUnit& a) { a.loco.dir ^= 1; }},
            {"loco.serial", [](AntUnit& a) { a.loco.serial += 1; }},
            {"loco.evt5_ms", [](AntUnit& a) { a.loco.evt5_ms += 1; }},
            {"loco.sound_mask", [](AntUnit& a) { a.loco.sound_mask ^= 1; }},
            {"loco_action", [](AntUnit& a) { a.loco_action ^= 1; }},
            {"dive_flag", [](AntUnit& a) { a.dive_flag = !a.dive_flag; }},
            {"pause_active", [](AntUnit& a) { a.pause_active = !a.pause_active; }},
            {"pause_fire_ms", [](AntUnit& a) { a.pause_fire_ms += 1; }},
            {"pause_saved_action", [](AntUnit& a) { a.pause_saved_action += 1; }},
            {"pause_saved_dir", [](AntUnit& a) { a.pause_saved_dir += 1; }},
            {"orig_order", [](AntUnit& a) { a.orig_order ^= 1; }},
            {"orig_order_tile", [](AntUnit& a) { a.orig_order_tile.x += 1; }},
            {"home_state", [](AntUnit& a) { a.home_state ^= 1; }},
            {"home_priority", [](AntUnit& a) { a.home_priority ^= 1; }},
            {"home_time_ms", [](AntUnit& a) { a.home_time_ms += 1; }},
            {"raid_amount", [](AntUnit& a) { a.raid_amount += 1; }},
            {"orig_target_team", [](AntUnit& a) { a.orig_target_team ^= 1; }},
            {"orig_target_ant", [](AntUnit& a) { a.orig_target_ant += 1; }},
            {"orig_special_tile", [](AntUnit& a) { a.orig_special_tile.x += 1; }},
            {"orig_b4", [](AntUnit& a) { a.orig_b4 += 1; }},
            {"orig_food_id", [](AntUnit& a) { a.orig_food_id += 1; }},
            {"orig_food_tile", [](AntUnit& a) { a.orig_food_tile.x += 1; }},
            {"harvest_amount", [](AntUnit& a) { a.harvest_amount += 1; }},
            {"move_serial", [](AntUnit& a) { a.move_serial += 1; }},
            {"occ_tile", [](AntUnit& a) { a.occ_tile.x += 1; }},
            {"arrived_this_tick", [](AntUnit& a) { a.arrived_this_tick = !a.arrived_this_tick; }},
        };
        const StateHash base = w.sim.state_hash();
        std::string missed;
        for (const auto& c : cases) {
            AntUnit& u = w.sim.get_unit(id);
            const AntUnit before = u;
            c.change(u);
            if (w.sim.state_hash() == base) missed += std::string(" ") + c.name;
            u = before;
            if (w.sim.state_hash() != base) missed += std::string(" (restore ") + c.name + ")";
        }
        if (!missed.empty()) std::cout << "\n    not covered:" << missed << "\n";
        ASSERT_TRUE(missed.empty());
    } TEST_END();

    TEST_CASE("N1.13 State Hash Covers Every Field Of A Map Cell, The Hills, The Food, The Statistics And The Clocks") {
        World w;
        build_world(w, 1);
        for (int t = 0; t < 3; ++t) w.sim.tick();
        const StateHash base = w.sim.state_hash();
        std::string missed;
        auto probe = [&](const char* name, const std::function<void()>& change, const std::function<void()>& restore) {
            change();
            if (w.sim.state_hash() == base) missed += std::string(" ") + name;
            restore();
            if (w.sim.state_hash() != base) missed += std::string(" (restore ") + name + ")";
        };
        TileCell& cell = w.sim.grid_mut().get_cell_mut(10, 10);
        const TileCell original = cell;
        const std::vector<std::pair<const char*, std::function<void(TileCell&)>>> cell_cases = {
            {"terrain_id", [](TileCell& c) { c.terrain_id += 1; }},
            {"terrain_type", [](TileCell& c) { c.terrain_type ^= 1; }},
            {"surface_type", [](TileCell& c) { c.surface_type = SurfaceType::Mud; }},
            {"flags", [](TileCell& c) { c.flags ^= 1; }},
            {"interactive_id", [](TileCell& c) { c.interactive_id ^= 1; }},
            {"interactive_owner", [](TileCell& c) { c.interactive_owner ^= 1; }},
            {"timer_ticks", [](TileCell& c) { c.timer_ticks += 1; }},
            {"anchor_x", [](TileCell& c) { c.anchor_x += 1; }},
            {"anchor_y", [](TileCell& c) { c.anchor_y += 1; }},
            {"is_food", [](TileCell& c) { c.is_food = !c.is_food; }},
            {"is_mud", [](TileCell& c) { c.is_mud = !c.is_mud; }},
            {"is_powerup", [](TileCell& c) { c.is_powerup = !c.is_powerup; }},
            {"powerup_type", [](TileCell& c) { c.powerup_type ^= 1; }},
            {"is_obstacle_overlay", [](TileCell& c) { c.is_obstacle_overlay = !c.is_obstacle_overlay; }},
            {"is_thief_only", [](TileCell& c) { c.is_thief_only = !c.is_thief_only; }},
            {"is_corridor_team_locked", [](TileCell& c) { c.is_corridor_team_locked = !c.is_corridor_team_locked; }},
            {"is_base_hole", [](TileCell& c) { c.is_base_hole = !c.is_base_hole; }},
            {"base_owner_team", [](TileCell& c) { c.base_owner_team ^= 1; }},
            {"occupant_ant_id", [](TileCell& c) { c.occupant_ant_id += 1; }},
            {"static_solid", [](TileCell& c) { c.static_solid = !c.static_solid; }},
        };
        for (const auto& cc : cell_cases) {
            probe(cc.first, [&]() { cc.second(w.sim.grid_mut().get_cell_mut(10, 10)); }, [&]() { w.sim.grid_mut().get_cell_mut(10, 10) = original; });
        }
        // the map's last cell (the whole grid is hashed, not a prefix)
        const TileCell last = w.sim.grid().get_cell(59, 59);
        probe("last cell", [&]() { w.sim.grid_mut().get_cell_mut(59, 59).terrain_id += 1; }, [&]() { w.sim.grid_mut().get_cell_mut(59, 59) = last; });
        // hills
        {
            const uint16_t old_x = w.sim.grid_mut().anthills_mut()[0].x;
            probe("hill x", [&]() { w.sim.grid_mut().anthills_mut()[0].x += 1; }, [&]() { w.sim.grid_mut().anthills_mut()[0].x = old_x; });
            const uint8_t old_team = w.sim.grid_mut().anthills_mut()[1].team_id;
            probe("hill team", [&]() { w.sim.grid_mut().anthills_mut()[1].team_id ^= 1; }, [&]() { w.sim.grid_mut().anthills_mut()[1].team_id = old_team; });
        }
        // statistics, clocks
        auto probe2 = [&](const char* name, const std::function<void()>& change) {
            const StateHash before = w.sim.state_hash();
            change();
            if (w.sim.state_hash() == before) missed += std::string(" ") + name;
        };
        probe2("food remaining", [&]() {                       // the lunchbox at (20, 20) loses its unit
            bool stage = false;
            const int32_t obj = w.sim.grid().food_object_at_cell(TileCoord{20, 20});
            w.sim.grid_mut().take_food(obj, 1, stage);
        });
        probe2("food object added", [&]() {
            FoodObject o;
            o.row = 40;
            o.col = 40;
            o.units = 2;
            o.value = 10;
            o.remaining = 2;
            o.thresholds = {2, 0};
            o.stage_tiles = {369, 0x7FFE};
            w.sim.grid_mut().add_food_object(o);
        });
        probe2("score", [&]() { w.sim.set_player_score(1, w.sim.get_player_score(1) + 7); });
        probe2("eggs", [&]() { w.sim.set_player_eggs(2, w.sim.get_player_eggs(2) + 1); });
        probe2("clock", [&]() { w.sim.set_match_time_remaining_ms(w.sim.get_match_time_remaining_ms() - 50); });
        probe2("alliance", [&]() { w.sim.form_alliance(0, 3); });
        probe2("friendly_lost", [&]() { w.sim.record_player_stat(1, StatType::FriendlyLost, 5); });
        probe2("enemy_killed", [&]() { w.sim.record_player_stat(1, StatType::EnemyKilled, 5); });
        probe2("hatched", [&]() { w.sim.record_player_stat(1, StatType::NewHatched, 5); });
        probe2("pending invite", [&]() { w.sim.propose_alliance(1, 2); });
        probe2("hatch state", [&]() { w.sim.set_player_score(0, 900); w.sim.try_hatch(0); });
        probe2("hatch delay", [&]() { w.sim.set_hatch_delay_ticks(77); });
        probe2("fire wall timer", [&]() { w.sim.set_fire_at(TileCoord{12, 12}, 500); });
        probe2("bridge", [&]() { w.sim.set_bridge_at(TileCoord{30, 12}, 2, 900); });
        probe2("a new ant", [&]() { w.sim.spawn_unit(3, AntType::Worker, TileCoord{40, 40}); });
        if (!missed.empty()) std::cout << "\n    not covered:" << missed << "\n";
        ASSERT_TRUE(missed.empty());
    } TEST_END();
}

// ---------------------------------------------------------------------------------------------------------------------------------

bool load_map(const char* file, ants::assets::LevelData& lvl) {
    return lvl.load_lvl(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/" + file);
}

void run_reset_tests() {
    TEST_CASE("N1.14 init(): An Engine That Played Another Map Equals A Fresh Engine (every subsystem hash, then 200 ticks)") {
        const char* maps[] = {"TREASURE.LVL", "SMALL.LVL", "TINY.LVL", "MEDIUM.LVL", "ISLANDS.LVL", "GAUNTLET.LVL"};
        for (const char* first : maps) {
            ants::assets::LevelData l1;
            ASSERT_TRUE(load_map(first, l1));
            for (const char* second : maps) {
                if (first == second) continue;
                ants::assets::LevelData l2;
                ASSERT_TRUE(load_map(second, l2));
                SimulationEngine reused;
                reused.init(l1, 11);
                for (int t = 0; t < 30; ++t) reused.tick();
                reused.apply_command(make_command(CommandType::GroupMove, 0, 255, 5, 5, {1}));
                reused.init(l2, 99);
                SimulationEngine fresh;
                fresh.init(l2, 99);
                const StateHash a = reused.state_hash();
                const StateHash b = fresh.state_hash();
                if (a != b) {
                    std::cout << "\n    " << first << " then " << second << ": engine " << (a.engine == b.engine) << " players " << (a.players == b.players)
                              << " grid " << (a.grid == b.grid) << " food " << (a.food == b.food) << " ants " << (a.ants == b.ants) << " paths "
                              << (a.paths == b.paths) << " droppers " << (a.droppers == b.droppers) << "\n";
                }
                ASSERT_TRUE(a == b);
                for (int t = 0; t < 200; ++t) {
                    reused.tick();
                    fresh.tick();
                }
                ASSERT_TRUE(reused.state_hash() == fresh.state_hash());
            }
        }
    } TEST_END();

    TEST_CASE("N1.15 init_test_world(): The Same Holds For Test Worlds Of Different Sizes") {
        SimulationEngine reused;
        reused.init_test_world(60, 60, 5);
        reused.spawn_unit(0, AntType::Worker, TileCoord{3, 3});
        reused.grid_mut().set_anthill(1, TileCoord{10, 10});
        for (int t = 0; t < 20; ++t) reused.tick();
        reused.init_test_world(30, 20, 9);
        SimulationEngine fresh;
        fresh.init_test_world(30, 20, 9);
        ASSERT_TRUE(reused.state_hash() == fresh.state_hash());
    } TEST_END();
}

}  // namespace

int main() {
    std::cout << "\n=======================================================\n [SUITE] Network port, milestone 1: commands and the state hash\n"
                 "=======================================================\n";
    run_codec_tests();
    run_validation_tests();
    run_lockstep_tests();
    run_coverage_tests();
    run_reset_tests();
    std::cout << "\n=======================================================\n Total Test Cases: " << g_test_count << "\n Total Assertions: " << g_assert_count
              << "\n Failed:           " << g_test_failures << "\n=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}
