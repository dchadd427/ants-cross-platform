// Tests of the command layer and the state hash of the network port (docs/GAME_REVERSE_ENGINEERING.md, "Network port"): a Command is the
// intent of one player as plain data; SimulationEngine::apply_command validates it (issuer, ownership, ranges) and applies it; two engines that
// apply the same commands at the same ticks in canonical order stay bit-identical, which state_hash() proves every tick.
#include "ants_assets/lvl_parser.hpp"
#include "ants_sim/command.hpp"
#include "ants_sim/game_strings.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_sim/start_teams.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <limits>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "ants_test_paths.hpp"

using namespace ants::sim;

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
            c.type = static_cast<CommandType>(1 + rng.below(11));               // every type including the system command Drop
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

    TEST_CASE("N1.9c The News Of The Alliance Protocol Say Which Seat They Are About (NewsEvent::subject, presentation only): the seat that answered an invitation (accepted, rejected) or withdrew it, for the one who is told; 255 for every other news") {
        World w;
        build_world(w, 1);
        // the (target, subject) pairs of the events with that text since the last look; everything else that was posted is dropped
        const auto news_of = [&](uint16_t string_id) {
            std::vector<std::pair<int, int>> out;
            for (const NewsEvent& n : w.sim.poll_news_events()) {
                if (n.string_id == string_id) out.emplace_back(n.target_player, n.subject);
            }
            return out;
        };
        using Pairs = std::vector<std::pair<int, int>>;
        w.sim.poll_news_events();
        ASSERT_EQ(w.sim.apply_command(make_command(CommandType::AllianceInvite, 2, 3)).status, Status::Applied);
        ASSERT_EQ(w.sim.apply_command(make_command(CommandType::AllianceDeny, 3, 2)).status, Status::Applied);
        ASSERT_TRUE(news_of(strings::kTeamRejected) == (Pairs{{2, 3}}));                       // told to the proposer (2), about the seat that said no (3)
        ASSERT_EQ(w.sim.apply_command(make_command(CommandType::AllianceInvite, 0, 1)).status, Status::Applied);
        w.sim.poll_news_events();
        ASSERT_EQ(w.sim.apply_command(make_command(CommandType::AllianceAccept, 1, 0)).status, Status::Applied);
        ASSERT_TRUE(news_of(strings::kTeamAccepted) == (Pairs{{0, 1}}));                       // told to the proposer (0), about the seat that said yes (1)
        ASSERT_EQ(w.sim.apply_command(make_command(CommandType::AllianceInvite, 2, 3)).status, Status::Applied);
        w.sim.poll_news_events();
        ASSERT_EQ(w.sim.apply_command(make_command(CommandType::AllianceWithdraw, 2, 3)).status, Status::Applied);
        ASSERT_TRUE(news_of(strings::kTeamWithdrawn) == (Pairs{{3, 2}}));                      // told to the invited seat (3), about the proposer that withdrew (2)
        // any other news names no seat
        w.sim.poll_news_events();
        ASSERT_EQ(w.sim.apply_command(make_command(CommandType::AllianceInvite, 1, 2)).status, Status::Applied);
        size_t others = 0;
        for (const NewsEvent& n : w.sim.poll_news_events()) {
            if (n.string_id != strings::kTeamRejected && n.string_id != strings::kTeamAccepted && n.string_id != strings::kTeamWithdrawn) {
                ASSERT_EQ(n.subject, 255);
                ++others;
            }
        }
        ASSERT_TRUE(others > 0);                                                              // (the invitation's own news was looked at: it is not one of the three)
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
    TEST_CASE("N1.9b Dropper Power-Up Pick: The Conversion Is __ftol (Truncate To 64 Bits, Keep The Low 32), Defined For Every Probability A Map File Can Hold (0x1034580)") {
        // ordinary probabilities: 0.25 -> 2500 of 10000; a zero entry is skipped
        ASSERT_EQ(static_cast<int>(SimulationEngine::pick_dropper_powerup({0.25, 0.0, 0.5, 0.25, 0.0}, 0)), 0);
        ASSERT_EQ(static_cast<int>(SimulationEngine::pick_dropper_powerup({0.25, 0.0, 0.5, 0.25, 0.0}, 2499)), 0);
        ASSERT_EQ(static_cast<int>(SimulationEngine::pick_dropper_powerup({0.25, 0.0, 0.5, 0.25, 0.0}, 2500)), 2);
        ASSERT_EQ(static_cast<int>(SimulationEngine::pick_dropper_powerup({0.25, 0.0, 0.5, 0.25, 0.0}, 7499)), 2);
        ASSERT_EQ(static_cast<int>(SimulationEngine::pick_dropper_powerup({0.25, 0.0, 0.5, 0.25, 0.0}, 7500)), 3);
        ASSERT_EQ(static_cast<int>(SimulationEngine::pick_dropper_powerup({0.25, 0.0, 0.5, 0.25, 0.0}, 10000)), 0xFF);
        // the community map with probabilities of 1e13 and more (4.2e17 of 10000 does not fit an int): the sums are the low 32 bits of the 64-bit truncations,
        // 1437204480, 933429248 (so -1924333568 as a signed sum), ... on every platform
        const std::array<double, 5> wild{4.2e13, 9.9e13, 0.0, 1.4e14, 2.8e13};
        ASSERT_EQ(static_cast<int>(SimulationEngine::pick_dropper_powerup(wild, 0)), 0);                    // 0 < 1437204480
        ASSERT_EQ(static_cast<int>(SimulationEngine::pick_dropper_powerup(wild, 1437204479)), 0);
        ASSERT_EQ(static_cast<int>(SimulationEngine::pick_dropper_powerup(wild, 1437204480)), 0xFF);        // not below the first sum; the later sums are negative as signed 32-bit values: nothing is picked
        // out of the 64-bit range, NaN, infinities: the indefinite value, low half 0 (no undefined behaviour)
        ASSERT_EQ(static_cast<int>(SimulationEngine::pick_dropper_powerup({1e300, 0.0, 0.0, 0.0, 0.0}, 0)), 0xFF);      // adds 0: nothing is below 0 ...
        ASSERT_EQ(static_cast<int>(SimulationEngine::pick_dropper_powerup({std::numeric_limits<double>::quiet_NaN(), 0.5, 0.0, 0.0, 0.0}, 100)), 1);
        ASSERT_EQ(static_cast<int>(SimulationEngine::pick_dropper_powerup({std::numeric_limits<double>::infinity(), -1e300, 0.5, 0.0, 0.0}, 100)), 2);
    } TEST_END();

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
        // the level's default ant type (block 3): state that decides what every worker is
        probe("default ant tile", [&]() { w.sim.grid_mut().set_default_ant_tile(62); }, [&]() { w.sim.grid_mut().set_default_ant_tile(0x7FFE); });
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

// ---------------------------------------------------------------------------------------------------------------------------------
// Roster, drop-out and the predicted acknowledgement of the network integration
// ---------------------------------------------------------------------------------------------------------------------------------

int popcount4(uint8_t m) {
    int n = 0;
    for (int i = 0; i < 4; ++i) n += (m >> i) & 1;
    return n;
}

bool load_map_file(const char* file, ants::assets::LevelData& lvl) {
    return lvl.load_lvl(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/" + file);
}

void run_roster_tests() {
    TEST_CASE("N1.16 Roster: A Team Without A Player Has No Hill, No Start Markers (Ants) And No Eggs; It Cannot Command; The Roster Is Hashed") {
        const char* maps[] = {"TREASURE.LVL", "SMALL.LVL", "TINY.LVL", "MEDIUM.LVL", "ISLANDS.LVL", "GAUNTLET.LVL"};
        const uint8_t rosters[] = {0x03, 0x05, 0x09, 0x06, 0x0A, 0x0C, 0x07, 0x0D, 0x0E, 0x01, 0x02, 0x08};
        for (const char* file : maps) {
            ants::assets::LevelData lvl;
            ASSERT_TRUE(load_map_file(file, lvl));
            SimulationEngine full;
            full.init(lvl, 5);
            ASSERT_EQ(full.roster_mask(), 0x0F);
            const size_t full_hills = full.grid().anthills().size();
            const uint32_t full_eggs = full.get_world_state().player_eggs[0];
            ASSERT_TRUE(full_eggs > 0);
            StateHash previous = full.state_hash();
            for (uint8_t roster : rosters) {
                SimulationEngine sim;
                sim.init(lvl, 5, roster);
                ASSERT_EQ(sim.roster_mask(), roster);
                const WorldState& world = sim.get_world_state();
                for (const auto& hill : sim.grid().anthills()) ASSERT_TRUE((roster >> hill.team_id) & 1);
                ASSERT_EQ(sim.grid().anthills().size(), full_hills >= 4 ? static_cast<size_t>(popcount4(roster)) : std::min<size_t>(full_hills, static_cast<size_t>(popcount4(roster))));
                for (const auto& a : world.ants) ASSERT_TRUE((roster >> a.player_id) & 1);       // no ant of an absent team
                bool any_ant[4] = {false, false, false, false};
                for (const auto& a : world.ants) any_ant[a.player_id] = true;
                for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
                    const bool present = (roster >> p) & 1;
                    ASSERT_EQ(any_ant[p], present);                                           // every present team starts with its ants
                    ASSERT_EQ(world.player_eggs[p], present ? full_eggs : 0u);
                    const uint8_t cmd_issuer = p;
                    const Status st = sim.apply_command(make_command(CommandType::Hatch, cmd_issuer)).status;
                    ASSERT_EQ(st, present ? Status::Applied : Status::RejectedIssuer);       // a team without a player has no voice
                }
                // the mound of an absent team is plain ground: no owner, no hole, no thief tile
                for (const auto& hill : full.grid().anthills()) {
                    if ((roster >> hill.team_id) & 1) continue;
                    for (int dy = 0; dy < 4; ++dy) {
                        for (int dx = 0; dx < 4; ++dx) {
                            const TileCell& c = sim.grid().get_cell(TileCoord{hill.x + dx, hill.y + dy});
                            ASSERT_EQ(c.base_owner_team, 255);
                            ASSERT_FALSE(c.is_base_hole);
                            ASSERT_FALSE(c.is_thief_only);
                        }
                    }
                }
                // the roster is part of the state, and equal rosters give equal states
                SimulationEngine twin;
                twin.init(lvl, 5, roster);
                ASSERT_TRUE(twin.state_hash() == sim.state_hash());
                ASSERT_TRUE(sim.state_hash() != full.state_hash());
                ASSERT_TRUE(sim.state_hash() != previous);
                previous = sim.state_hash();
            }
        }
    } TEST_END();

    TEST_CASE("N1.17 Roster: A Match With Three Teams Runs Deterministically And An Engine Reused After A Roster Match Equals A Fresh One") {
        ants::assets::LevelData lvl;
        ASSERT_TRUE(load_map_file("TREASURE.LVL", lvl));
        SimulationEngine a;
        SimulationEngine b;
        a.init(lvl, 77, 0x0B);
        b.init(lvl, 77, 0x0B);
        for (int t = 0; t < 400; ++t) {
            if (t % 20 == 3) {
                for (uint8_t p : {uint8_t{0}, uint8_t{1}, uint8_t{3}}) {
                    std::vector<uint32_t> mine;
                    for (const auto& ant : a.get_world_state().ants) {
                        if (ant.player_id == p) mine.push_back(ant.id);
                    }
                    if (mine.empty()) continue;
                    Lcg rng(static_cast<uint32_t>(t) * 31u + p);
                    const Command c = make_command(CommandType::GroupMove, p, 255, static_cast<int16_t>(rng.below(60)), static_cast<int16_t>(rng.below(60)), mine);
                    a.apply_command(c);
                    b.apply_command(c);
                }
            }
            a.tick();
            b.tick();
            ASSERT_TRUE(a.state_hash() == b.state_hash());
        }
        // a reused engine: after a roster match the next full-roster init equals a fresh engine (mask, drops and eggs are reset)
        SimulationEngine fresh;
        fresh.init(lvl, 9);
        a.drop_player(1);
        a.init(lvl, 9);
        ASSERT_EQ(a.roster_mask(), 0x0F);
        ASSERT_FALSE(a.is_player_dropped(1));
        ASSERT_TRUE(a.state_hash() == fresh.state_hash());
    } TEST_END();
}

void run_drop_tests() {
    TEST_CASE("N1.18 Drop: The Team Leaves (FUN_0100d03b): Text 46, The Cue, Its Ants Die, Its Alliance Ends, Its Egg Is Lost, Nothing Hatches") {
        World w;
        build_world(w, 3);
        w.sim.set_player_name(1, "Redd");
        w.sim.form_alliance(1, 2);
        w.sim.set_player_score(1, 900);
        ASSERT_EQ(w.sim.try_hatch(1), SimulationEngine::HatchResult::Started);
        ASSERT_EQ(w.sim.get_pending_hatch_count(1), 1u);
        const uint32_t eggs_before = w.sim.get_player_eggs(1);
        w.sim.clear_audio_events();
        w.sim.clear_news_events();
        ASSERT_FALSE(w.sim.is_player_dropped(1));
        const CommandResult r = w.sim.apply_command(make_command(CommandType::Drop, 1));
        ASSERT_EQ(r.status, Status::Applied);
        ASSERT_TRUE(w.sim.is_player_dropped(1));
        ASSERT_FALSE(w.sim.is_player_dropped(0));
        ASSERT_TRUE(w.sim.has_audio_event(SoundID::PlayerDropOut));                 // playerout.wav
        ASSERT_TRUE(w.sim.has_news_event(0, 46));                                   // "%s dropped out of the game!"
        bool named = false;
        for (const NewsEvent& n : w.sim.poll_news_events()) named = named || n.message_text.find("Redd") != std::string::npos;
        ASSERT_TRUE(named);
        ASSERT_EQ(w.sim.get_ally_id(1), ALLIANCE_NONE);
        ASSERT_EQ(w.sim.get_ally_id(2), ALLIANCE_NONE);
        ASSERT_EQ(w.sim.get_pending_hatch_count(1), 0u);
        const size_t others = [&]() { size_t n = 0; for (const auto& a : w.sim.get_world_state().ants) n += a.player_id != 1; return n; }();
        for (int t = 0; t < 120; ++t) w.sim.tick();
        size_t left_of_1 = 0;
        size_t left_of_others = 0;
        for (const auto& a : w.sim.get_world_state().ants) {
            if (a.player_id == 1) ++left_of_1; else ++left_of_others;
        }
        ASSERT_EQ(left_of_1, 0u);                                                   // every ant of the team died through the death clip
        ASSERT_EQ(left_of_others, others);                                          // and nobody else was touched
        ASSERT_EQ(w.sim.get_pending_hatch_count(1), 0u);                            // CheckNoAnts does not hatch for a dropped team
        ASSERT_EQ(w.sim.get_player_eggs(1), eggs_before);                           // the eggs stay unused
        // the team no longer acts, and a second drop changes nothing
        ASSERT_EQ(w.sim.apply_command(make_command(CommandType::GroupMove, 1, 255, 10, 10, {w.ants[1][0]})).status, Status::Ignored);
        ASSERT_EQ(w.sim.apply_command(make_command(CommandType::Hatch, 1)).status, Status::Ignored);
        ASSERT_EQ(w.sim.apply_command(make_command(CommandType::Drop, 1)).status, Status::Ignored);
        const StateHash h = w.sim.state_hash();
        w.sim.drop_player(1);
        ASSERT_TRUE(h == w.sim.state_hash());
        // the others still play
        ASSERT_EQ(w.sim.apply_command(make_command(CommandType::Hatch, 0)).status, Status::Applied);
    } TEST_END();

    TEST_CASE("N1.19 Drop: Two Engines That Drop A Team In The Same Tick Stay Bit-Identical; A Drop Changes The State; Bad Drops Are Refused") {
        World a;
        World b;
        World c;
        build_world(a, 8);
        build_world(b, 8);
        build_world(c, 8);
        for (int t = 0; t < 300; ++t) {
            if (t == 60) {
                ASSERT_EQ(a.sim.apply_command(make_command(CommandType::Drop, 2)).status, Status::Applied);
                ASSERT_EQ(b.sim.apply_command(make_command(CommandType::Drop, 2)).status, Status::Applied);
                ASSERT_TRUE(a.sim.state_hash() != c.sim.state_hash());
            }
            for (const Command& cmd : script_for_tick(a, 8, static_cast<uint32_t>(t))) {
                a.sim.apply_command(cmd);
                b.sim.apply_command(cmd);
                c.sim.apply_command(cmd);
            }
            a.sim.tick();
            b.sim.tick();
            c.sim.tick();
            ASSERT_TRUE(a.sim.state_hash() == b.sim.state_hash());
        }
        ASSERT_TRUE(a.sim.state_hash() != c.sim.state_hash());
        // a drop of a player that does not exist or is not in the match
        ASSERT_EQ(a.sim.apply_command(make_command(CommandType::Drop, 4)).status, Status::RejectedIssuer);
        ants::assets::LevelData lvl;
        ASSERT_TRUE(load_map_file("SMALL.LVL", lvl));
        SimulationEngine three;
        three.init(lvl, 1, 0x07);
        ASSERT_EQ(three.apply_command(make_command(CommandType::Drop, 3)).status, Status::RejectedIssuer);
        three.drop_player(3);                                                       // not in the match: nothing
        ASSERT_FALSE(three.is_player_dropped(3));
        // the system command is not a client command; every other command is
        ASSERT_FALSE(is_client_command(CommandType::Drop));
        ASSERT_FALSE(is_client_command(CommandType::None));
        for (uint8_t t = 1; t <= 11; ++t) ASSERT_TRUE(is_client_command(static_cast<CommandType>(t)));          // 11 = Quit: the quit dialog's Yes is a player's command
        ASSERT_EQ(static_cast<uint8_t>(CommandType::Quit), 11);
        ASSERT_EQ(static_cast<uint8_t>(CommandType::Drop), 12);
        ASSERT_TRUE(CommandType::Last == CommandType::Drop);
        // the wire form of a drop has no ant list
        std::vector<uint8_t> bytes;
        encode(make_command(CommandType::Drop, 2), bytes);
        Command back;
        ASSERT_EQ(decode(bytes.data(), bytes.size(), back), DecodeError::None);
        ASSERT_EQ(back.type, CommandType::Drop);
        ASSERT_EQ(back.issuer, 2);
        // the wire form of a quit has no ant list either
        bytes.clear();
        encode(make_command(CommandType::Quit, 1), bytes);
        ASSERT_EQ(bytes.size(), kCommandHeaderBytes);
        ASSERT_EQ(decode(bytes.data(), bytes.size(), back), DecodeError::None);
        ASSERT_EQ(back.type, CommandType::Quit);
        ASSERT_EQ(back.issuer, 1);
    } TEST_END();

    TEST_CASE("N1.22 Quit: Two Engines That Apply The Same Quit Stay Bit-Identical; The Quitter Is Part Of The State; With Several Sides Left It Is A Drop") {
        World a;
        World b;
        World c;
        build_world(a, 9);
        build_world(b, 9);
        build_world(c, 9);
        for (int t = 0; t < 40; ++t) { a.sim.tick(); b.sim.tick(); c.sim.tick(); }
        ASSERT_TRUE(a.sim.state_hash() == c.sim.state_hash());
        ASSERT_EQ(a.sim.other_sides(1), 3u);                                        // four teams: a quit is a drop-out, the match goes on
        ASSERT_EQ(a.sim.apply_command(make_command(CommandType::Quit, 1)).status, Status::Applied);
        ASSERT_EQ(b.sim.apply_command(make_command(CommandType::Quit, 1)).status, Status::Applied);
        ASSERT_TRUE(a.sim.is_player_dropped(1));
        ASSERT_FALSE(a.sim.is_match_over());
        ASSERT_TRUE(a.sim.state_hash() == b.sim.state_hash());
        ASSERT_TRUE(a.sim.state_hash() != c.sim.state_hash());
        // the last other side: drop two more teams of one engine, the quit of the third ends the match and names the quitter
        a.sim.drop_player(2);
        b.sim.drop_player(2);
        ASSERT_FALSE(a.sim.is_match_over());
        ASSERT_EQ(a.sim.other_sides(0), 1u);
        ASSERT_EQ(a.sim.apply_command(make_command(CommandType::Quit, 0)).status, Status::Applied);
        ASSERT_TRUE(a.sim.is_match_over());
        ASSERT_EQ(a.sim.quitter(), 0);
        ASSERT_FALSE(a.sim.is_player_dropped(0));
        ASSERT_TRUE(a.sim.state_hash() != b.sim.state_hash());                      // b has not had the quit yet
        ASSERT_EQ(b.sim.apply_command(make_command(CommandType::Quit, 0)).status, Status::Applied);
        ASSERT_TRUE(a.sim.state_hash() == b.sim.state_hash());
        for (int t = 0; t < 10; ++t) { a.sim.tick(); b.sim.tick(); }                 // the match stays over and the engines stay equal
        ASSERT_TRUE(a.sim.state_hash() == b.sim.state_hash());
    } TEST_END();
}

void run_prediction_tests() {
    TEST_CASE("N1.20 predict_order_ack: The Closest Eligible Ant, The Skip Rules And The Refusals Of GoTo, Without Changing Anything") {
        World w;
        build_world(w, 1);
        // three workers of player 0 at different distances from a tile
        const uint32_t far_ant = w.sim.spawn_unit(0, AntType::Worker, TileCoord{10, 20});
        const uint32_t near_ant = w.sim.spawn_unit(0, AntType::Worker, TileCoord{18, 20});
        const uint32_t mid_ant = w.sim.spawn_unit(0, AntType::Worker, TileCoord{14, 20});
        const StateHash before = w.sim.state_hash();
        Command mv = make_command(CommandType::GroupMove, 0, 255, 22, 20, {far_ant, near_ant, mid_ant});
        ASSERT_EQ(w.sim.predict_order_ack(mv), near_ant);
        ASSERT_TRUE(w.sim.state_hash() == before);                                  // a prediction changes nothing
        // two equally close ants: the first of the list (the exchange sort keeps it in front)
        const uint32_t twin_a = w.sim.spawn_unit(0, AntType::Worker, TileCoord{18, 21});
        mv.ants = {twin_a, near_ant};
        ASSERT_EQ(w.sim.predict_order_ack(mv), twin_a);
        mv.ants = {near_ant, twin_a};
        ASSERT_EQ(w.sim.predict_order_ack(mv), near_ant);
        // the prediction is what the engine then does
        mv.ants = {far_ant, near_ant, mid_ant};
        const uint32_t predicted = w.sim.predict_order_ack(mv);
        ASSERT_EQ(w.sim.apply_command(mv).ack_ant, predicted);
        // the same order again: whatever the engine answers is what was predicted (ants whose goal was moved by the ring scan are not skipped)
        const uint32_t again = w.sim.predict_order_ack(mv);
        ASSERT_EQ(w.sim.apply_command(mv).ack_ant, again);
        // one ant, open ground: the ant carries the order out already, so the repeated click is skipped (nobody answers, predicted and real)
        const Command solo = make_command(CommandType::GroupMove, 0, 255, 40, 40, {near_ant});
        ASSERT_EQ(w.sim.predict_order_ack(solo), near_ant);
        ASSERT_EQ(w.sim.apply_command(solo).ack_ant, near_ant);
        ASSERT_EQ(w.sim.get_unit(near_ant).orig_order_tile, (TileCoord{40, 40}));
        ASSERT_EQ(w.sim.predict_order_ack(solo), 0u);
        ASSERT_EQ(w.sim.apply_command(solo).ack_ant, 0u);
        // foreign, unknown and repeated ids are ignored like apply_command does
        mv.ants = {w.ants[1][0], 99999, far_ant, far_ant};
        Command other = mv;
        other.tile_x = 23;
        ASSERT_EQ(w.sim.predict_order_ack(other), far_ant);
        ASSERT_EQ(w.sim.apply_command(other).ack_ant, far_ant);
        // an enemy hill is no goal for a worker (GoTo stops the ant): no answer; a thief may raid it
        const TileCoord enemy_hill = TileCoord{w.sim.grid().find_anthill(1)->x, w.sim.grid().find_anthill(1)->y};
        const uint32_t thief = w.ants[0][3];
        const uint32_t worker = w.ants[0][0];
        Command onto_hill = make_command(CommandType::GroupMove, 0, 255, static_cast<int16_t>(enemy_hill.x + 1), static_cast<int16_t>(enemy_hill.y + 1), {worker});
        ASSERT_EQ(w.sim.predict_order_ack(onto_hill), 0u);
        ASSERT_EQ(w.sim.apply_command(onto_hill).ack_ant, 0u);
        onto_hill.ants = {thief};
        ASSERT_EQ(w.sim.predict_order_ack(onto_hill), thief);
        ASSERT_EQ(w.sim.apply_command(onto_hill).ack_ant, thief);
        // malformed, foreign issuers and finished matches predict nothing
        ASSERT_EQ(w.sim.predict_order_ack(make_command(CommandType::Stop, 0, 255, 0, 0, {far_ant})), 0u);
        ASSERT_EQ(w.sim.predict_order_ack(make_command(CommandType::GroupMove, 0, 255, -1, 5, {far_ant})), 0u);
        ASSERT_EQ(w.sim.predict_order_ack(make_command(CommandType::GroupMove, 0, 255, 5, 500, {far_ant})), 0u);
        ASSERT_EQ(w.sim.predict_order_ack(make_command(CommandType::GroupMove, 9, 255, 5, 5, {far_ant})), 0u);
        ASSERT_EQ(w.sim.predict_order_ack(make_command(CommandType::GroupMove, 0, 255, 5, 5, {})), 0u);
        // a special order needs a valid target for the ant's type: a bomber onto plain ground is fine (plant), onto water is not
        const uint32_t bomber = w.ants[0][1];
        ASSERT_EQ(w.sim.predict_order_ack(make_command(CommandType::GroupSpecial, 0, 255, 25, 25, {bomber})), bomber);   // the bomb tile
        ASSERT_EQ(w.sim.predict_order_ack(make_command(CommandType::GroupSpecial, 0, 255, 30, 10, {bomber})), 0u);       // river water
        // the skip rules follow the ant's order (0x1028874 / 0x10288b2): a special click on the tile of a plain walk is not skipped, a repeated
        // special click on the target of the ability order is; predicted and real answers agree at every step
        TileCoord free_ground{-1, -1};
        for (int32_t y = 20; y < 50 && free_ground.x < 0; ++y) {
            for (int32_t x = 20; x < 50; ++x) {
                const TileCoord t{x, y};
                if (w.sim.is_special_target_valid(AntType::Bomber, t, false, 0) && !w.sim.has_bomb_at(t) &&
                    w.sim.grid().get_cell(t).is_empty_overlay() && !w.sim.has_living_ant_at(t)) {
                    free_ground = t;
                    break;
                }
            }
        }
        ASSERT_TRUE(free_ground.x >= 0);
        const Command walk_there = make_command(CommandType::GroupMove, 0, 255, static_cast<int16_t>(free_ground.x), static_cast<int16_t>(free_ground.y), {bomber});
        const Command plant_there = make_command(CommandType::GroupSpecial, 0, 255, static_cast<int16_t>(free_ground.x), static_cast<int16_t>(free_ground.y), {bomber});
        ASSERT_EQ(w.sim.apply_command(walk_there).ack_ant, bomber);
        ASSERT_EQ(w.sim.get_unit(bomber).orig_order, AntUnit::kOrderMove);
        ASSERT_EQ(w.sim.predict_order_ack(walk_there), 0u);                          // the same plain click: skipped
        ASSERT_EQ(w.sim.predict_order_ack(plant_there), bomber);                     // a special click on that tile: a new order
        ASSERT_EQ(w.sim.apply_command(plant_there).ack_ant, bomber);
        ASSERT_EQ(w.sim.get_unit(bomber).orig_order, AntUnit::kOrderPlant);
        ASSERT_EQ(w.sim.predict_order_ack(plant_there), 0u);                         // the ant works on it now: skipped
        ASSERT_EQ(w.sim.apply_command(plant_there).ack_ant, 0u);
        ASSERT_EQ(w.sim.predict_order_ack(walk_there), bomber);                      // a plain click is not skipped by an ability order
        // a dropped team's orders predict nothing
        w.sim.drop_player(0);
        ASSERT_EQ(w.sim.predict_order_ack(mv), 0u);
    } TEST_END();

    TEST_CASE("N1.20b Group Orders: needing_order Counts The Ants That Needed The Click (FUN_010287b5 Returns 1 Even When Every GoTo Refuses, 0 When Every Ant Skipped); The Prediction Agrees") {
        World w;
        build_world(w, 1);
        const uint32_t a1 = w.sim.spawn_unit(0, AntType::Worker, TileCoord{18, 20});
        const uint32_t a2 = w.sim.spawn_unit(0, AntType::Worker, TileCoord{14, 20});
        uint32_t needed = 99;
        Command mv = make_command(CommandType::GroupMove, 0, 255, 40, 40, {a1, a2});
        ASSERT_TRUE(w.sim.predict_order_ack(mv, &needed) != 0);
        ASSERT_EQ(needed, 2u);
        CommandResult r = w.sim.apply_command(mv);
        ASSERT_EQ(r.needing_order, 2u);
        ASSERT_TRUE(r.ack_ant != 0);
        // an ant that already carries out this very click is skipped (a1, the closest, got the tile itself), the other one still needs it
        const uint32_t a3 = w.sim.spawn_unit(0, AntType::Worker, TileCoord{16, 21});
        ASSERT_EQ(w.sim.get_unit(a1).orig_order_tile, (TileCoord{40, 40}));
        Command again = make_command(CommandType::GroupMove, 0, 255, 40, 40, {a1, a3});
        w.sim.predict_order_ack(again, &needed);
        ASSERT_EQ(needed, 1u);
        ASSERT_EQ(w.sim.apply_command(again).needing_order, 1u);
        // every ant skipped: nobody needs it (the original returns 0: no voice, no pedestal feedback)
        const Command solo = make_command(CommandType::GroupMove, 0, 255, 40, 40, {a1});
        w.sim.predict_order_ack(solo, &needed);
        ASSERT_EQ(needed, 0u);
        r = w.sim.apply_command(solo);
        ASSERT_EQ(r.needing_order, 0u);
        ASSERT_EQ(r.ack_ant, 0u);
        // a worker ordered onto an enemy hill: its GoTo refuses (no acknowledgement) but it needed the order: the pedestal feedback still happens
        const TileCoord enemy_hill = TileCoord{w.sim.grid().find_anthill(1)->x, w.sim.grid().find_anthill(1)->y};
        const Command onto_hill = make_command(CommandType::GroupMove, 0, 255, static_cast<int16_t>(enemy_hill.x + 1), static_cast<int16_t>(enemy_hill.y + 1), {a3});
        w.sim.predict_order_ack(onto_hill, &needed);
        ASSERT_EQ(needed, 1u);
        r = w.sim.apply_command(onto_hill);
        ASSERT_EQ(r.ack_ant, 0u);
        ASSERT_EQ(r.needing_order, 1u);
        // a command that is not a group order or names no ant of the issuer predicts nothing
        needed = 99;
        w.sim.predict_order_ack(make_command(CommandType::Stop, 0, 255, 0, 0, {a1}), &needed);
        ASSERT_EQ(needed, 0u);
        ASSERT_EQ(w.sim.apply_command(make_command(CommandType::GroupMove, 0, 255, 5, 5, {w.ants[1][0]})).needing_order, 0u);       // a foreign ant
    } TEST_END();

    TEST_CASE("N1.21 predict_order_ack: Whenever The Engine Acknowledges An Order The Prediction Named The Same Ant (2000 random orders of a scripted match)") {
        World w;
        build_world(w, 21);
        Lcg rng(12345);
        int predicted_ack = 0;
        int real_ack = 0;
        int exact = 0;
        for (int t = 0; t < 400; ++t) {
            for (int k = 0; k < 5; ++k) {
                const uint8_t p = static_cast<uint8_t>(rng.below(MAX_PLAYERS));
                std::vector<uint32_t> pick;
                for (uint32_t id : w.ants[p]) {
                    if (rng.below(2) != 0) pick.push_back(id);
                }
                if (pick.empty()) pick.push_back(w.ants[p][rng.below(6)]);
                const uint32_t kind = rng.below(6);
                const CommandType type = kind < 3 ? CommandType::GroupMove : (kind < 5 ? CommandType::GroupAttack : CommandType::GroupSpecial);
                const Command c = make_command(type, p, 255, static_cast<int16_t>(rng.below(60)), static_cast<int16_t>(rng.below(60)), pick);
                uint32_t predicted_needed = 77;
                const uint32_t predicted = w.sim.predict_order_ack(c, &predicted_needed);
                const CommandResult applied = w.sim.apply_command(c);
                const uint32_t real = applied.ack_ant;
                ASSERT_EQ(predicted_needed, applied.needing_order);                  // the count of the ants that needed the order is predicted exactly
                if (predicted != 0) {
                    ++predicted_ack;
                    bool mine = false;
                    for (uint32_t id : pick) mine = mine || id == predicted;
                    ASSERT_TRUE(mine);                                               // it names an ant of the command
                }
                if (real != 0) {
                    ++real_ack;
                    if (predicted != real) {
                        std::cout << "\n    MISMATCH type " << static_cast<int>(type) << " tile (" << c.tile_x << "," << c.tile_y << ") predicted " << predicted << " real " << real
                                  << " real type " << static_cast<int>(w.sim.get_unit(real).type) << " ants:";
                        for (uint32_t id : pick) std::cout << " " << id << "(t" << static_cast<int>(w.sim.get_unit(id).type) << ")";
                        std::cout << "\n";
                    }
                    ASSERT_EQ(predicted, real);                                      // the engine's ack is always the predicted one
                }
                if (predicted == real) ++exact;
            }
            w.sim.tick();
        }
        std::cout << "\n    predicted an ack for " << predicted_ack << " orders, the engine acked " << real_ack << ", identical answers " << exact << " of 2000\n    ";
        ASSERT_TRUE(real_ack > 200);
        ASSERT_TRUE(exact * 100 >= 2000 * 98);                                       // and it is rarely wrong the other way round (a goal that stays blocked)
    } TEST_END();
}

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

// ---------------------------------------------------------------------------------------------------------------------------------
// The teams a match starts with (ants_sim/start_teams.hpp, protocol 13): one model for a game on this computer, a room on the local network and a server's room
// ---------------------------------------------------------------------------------------------------------------------------------

void run_start_team_tests() {
    TEST_CASE("N1.23 Start teams as a model: ffa | A+B (two different seats, any case of ffa, nothing else), what a match of a roster makes of them (the pair, then the two other seats when both play; nothing, with the reason, for a seat that does not play, a pair of one seat or when the teams would be the whole match), the choices of a game against bots and of a room, and the words of a choice") {
        StartTeams t;
        std::string why;
        ASSERT_TRUE(parse_start_teams("0+1", t, why) && t.set && t.a == 0 && t.b == 1);
        ASSERT_TRUE(parse_start_teams("3+2", t, why) && t.set && t.a == 3 && t.b == 2);
        ASSERT_TRUE(parse_start_teams("ffa", t, why) && !t.set);
        ASSERT_TRUE(parse_start_teams("FFA", t, why) && !t.set && parse_start_teams("Ffa", t, why) && !t.set);
        for (const char* bad : {"", "0", "01", "0+", "+1", "0+0", "2+2", "0+4", "4+0", "9+1", "a+b", "0-1", "0 1", "0 + 1", " 0+1", "0+1 ", "0+1+2", "0+10", "ff", "ffaa", "free for all", "none", "-1+0"}) {
            t = StartTeams{true, 1, 2};
            why.clear();
            ASSERT_FALSE(parse_start_teams(bad, t, why));
            ASSERT_TRUE(t.set && t.a == 1 && t.b == 2);                                                    // (nothing changed)
            ASSERT_FALSE(why.empty());
        }
        ASSERT_EQ(start_teams_text(StartTeams{}), std::string("ffa"));
        ASSERT_EQ(start_teams_text(StartTeams{true, 0, 1}), std::string("0+1"));
        ASSERT_EQ(start_teams_text(StartTeams{true, 3, 2}), std::string("3+2"));
        ASSERT_TRUE(parse_start_teams(start_teams_text(StartTeams{true, 2, 0}), t, why) && t == StartTeams({true, 2, 0}));
        ASSERT_TRUE(StartTeams{} == StartTeams({false, 3, 1}));                                            // (free for all has no pair to compare)
        ASSERT_TRUE(StartTeams({true, 0, 1}) != StartTeams({true, 1, 0}));                                 // (the order of a pair is part of the choice: the first seat invites)

        const auto plan_of = [](const StartTeams& teams, uint8_t roster) {
            std::string out;
            const StartTeamsPlan plan = plan_start_teams(teams, roster);
            for (const auto& p : plan.pairs) out += std::to_string(p[0]) + "+" + std::to_string(p[1]) + " ";
            return out + "[" + plan.why + "]";
        };
        for (unsigned roster = 0; roster < 16; ++roster) {                                                 // whatever the teams and the roster: a pair or a reason, never both and never neither
            for (uint8_t a = 0; a < 6; ++a) {
                for (uint8_t b = 0; b < 6; ++b) {
                    const StartTeamsPlan plan = plan_start_teams(StartTeams{true, a, b}, static_cast<uint8_t>(roster));
                    ASSERT_TRUE(plan.pairs.empty() != plan.why.empty());
                    ASSERT_TRUE(plan.why.empty() == plan.short_why.empty());
                    ASSERT_TRUE(10 + plan.short_why.size() <= 100);                                       // "No teams: " + the reason is one line of chat (net::kMaxChatChars = 100)
                    ASSERT_TRUE(plan.pairs.empty() || (plan.pairs.size() <= 2 && plan.pairs[0][0] == a && plan.pairs[0][1] == b));
                }
            }
        }
        ASSERT_EQ(plan_of(StartTeams{}, 0x0F), std::string("[]"));
        ASSERT_EQ(plan_of(StartTeams{true, 0, 1}, 0x0F), std::string("0+1 2+3 []"));
        ASSERT_EQ(plan_of(StartTeams{true, 1, 0}, 0x0F), std::string("1+0 2+3 []"));
        ASSERT_EQ(plan_of(StartTeams{true, 2, 3}, 0x0F), std::string("2+3 0+1 []"));
        ASSERT_EQ(plan_of(StartTeams{true, 1, 3}, 0x0F), std::string("1+3 0+2 []"));
        ASSERT_EQ(plan_of(StartTeams{true, 0, 1}, 0x07), std::string("0+1 []"));
        ASSERT_EQ(plan_of(StartTeams{true, 1, 2}, 0x07), std::string("1+2 []"));
        ASSERT_EQ(plan_of(StartTeams{true, 0, 3}, 0x0B), std::string("0+3 []"));
        ASSERT_EQ(plan_of(StartTeams{true, 0, 3}, 0x07), std::string("[seat 3 does not play in this match.]"));
        ASSERT_EQ(plan_of(StartTeams{true, 3, 0}, 0x07), std::string("[seat 3 does not play in this match.]"));
        ASSERT_EQ(plan_of(StartTeams{true, 1, 2}, 0x0D), std::string("[seat 1 does not play in this match.]"));
        ASSERT_EQ(plan_of(StartTeams{true, 0, 1}, 0x00), std::string("[seat 0 does not play in this match.]"));
        ASSERT_EQ(plan_of(StartTeams{true, 0, 7}, 0xFF), std::string("[seat 7 does not play in this match.]"));      // (a seat that no match has)
        ASSERT_EQ(plan_of(StartTeams{true, 2, 2}, 0x0F), std::string("[a team needs two different seats.]"));          // (what a message or a struct can say and parse never does)
        ASSERT_EQ(plan_of(StartTeams{true, 0, 1}, 0x03), std::string("[these are the only two seats that play, and a match in which every team is allied ends at once.]"));
        ASSERT_EQ(plan_of(StartTeams{true, 0, 2}, 0x05), std::string("[these are the only two seats that play, and a match in which every team is allied ends at once.]"));
        ASSERT_EQ(plan_start_teams(StartTeams{true, 0, 1}, 0x03).short_why, std::string("only two seats play: a team of them would end the match at once."));
        // the line of chat says the seat as the pages do, by its colour (the long reason is for the command line, where the seats are digits)
        ASSERT_EQ(plan_start_teams(StartTeams{true, 0, 3}, 0x07).short_why, std::string("Black does not play in this match."));
        ASSERT_EQ(plan_start_teams(StartTeams{true, 3, 0}, 0x07).short_why, std::string("Black does not play in this match."));
        ASSERT_EQ(plan_start_teams(StartTeams{true, 1, 2}, 0x0D).short_why, std::string("Red does not play in this match."));
        ASSERT_EQ(plan_start_teams(StartTeams{true, 2, 0}, 0x0B).short_why, std::string("Blue does not play in this match."));
        ASSERT_EQ(plan_start_teams(StartTeams{true, 1, 0}, 0x00).short_why, std::string("Red does not play in this match."));              // (the first of the pair that does not play)
        ASSERT_EQ(plan_start_teams(StartTeams{true, 0, 7}, 0xFF).short_why, std::string("seat 7 does not play in this match."));          // (a seat that no match has has no colour)

        const auto choices_of = [](const std::vector<StartTeams>& choices) {
            std::string out;
            for (const StartTeams& c : choices) out += start_teams_text(c) + " ";
            return out;
        };
        ASSERT_EQ(choices_of(local_team_choices(0, 0x0E)), std::string("ffa 0+1 0+2 0+3 "));
        ASSERT_EQ(choices_of(local_team_choices(0, 0x06)), std::string("ffa 0+1 0+2 "));
        ASSERT_EQ(choices_of(local_team_choices(0, 0x02)), std::string("ffa "));
        ASSERT_EQ(choices_of(local_team_choices(2, 0x0B)), std::string("ffa 2+0 2+1 2+3 "));
        ASSERT_EQ(choices_of(local_team_choices(7, 0x0F)), std::string("ffa "));
        ASSERT_EQ(choices_of(room_team_choices(2)), std::string("ffa "));                                  // two players: only free for all
        ASSERT_EQ(choices_of(room_team_choices(3)), std::string("ffa 0+1 0+2 1+2 "));
        ASSERT_EQ(choices_of(room_team_choices(4)), std::string("ffa 0+1 0+2 0+3 "));
        ASSERT_EQ(choices_of(room_team_choices(1)), std::string("ffa "));
        ASSERT_EQ(choices_of(room_team_choices(0)), std::string("ffa "));
        ASSERT_EQ(choices_of(room_team_choices(5)), std::string("ffa "));
        for (uint8_t players = 2; players <= 4; ++players) {                                               // every choice that a room offers can be made by the seats it fills first
            const uint8_t roster = static_cast<uint8_t>((1u << players) - 1u);
            for (const StartTeams& c : room_team_choices(players)) ASSERT_TRUE(plan_start_teams(c, roster).why.empty());
        }
        for (uint8_t own = 0; own < 4; ++own) {
            for (uint8_t filled = 0; filled < 16; ++filled) {
                const uint8_t roster = static_cast<uint8_t>((filled & 0x0Fu) | (1u << own));
                for (const StartTeams& c : local_team_choices(own, filled)) ASSERT_TRUE(plan_start_teams(c, roster).why.empty());
            }
        }

        ASSERT_EQ(start_teams_title(StartTeams{}, 0x0F), std::string("Free for all"));
        ASSERT_EQ(start_teams_title(StartTeams{true, 0, 1}, 0x0F), std::string("Green + Red against Blue + Black"));
        ASSERT_EQ(start_teams_title(StartTeams{true, 0, 2}, 0x0F), std::string("Green + Blue against Red + Black"));
        ASSERT_EQ(start_teams_title(StartTeams{true, 0, 3}, 0x0F), std::string("Green + Black against Red + Blue"));
        ASSERT_EQ(start_teams_title(StartTeams{true, 0, 1}, 0x07), std::string("Green + Red against Blue"));
        ASSERT_EQ(start_teams_title(StartTeams{true, 0, 2}, 0x07), std::string("Green + Blue against Red"));
        ASSERT_EQ(start_teams_title(StartTeams{true, 1, 2}, 0x07), std::string("Red + Blue against Green"));
        ASSERT_EQ(start_teams_title(StartTeams{true, 2, 3}, 0x0F), std::string("Blue + Black against Green + Red"));
        ASSERT_EQ(start_teams_title(StartTeams{true, 0, 1}, 0x03), std::string("Green + Red"));              // (no other seat plays)
        ASSERT_EQ(start_teams_title(StartTeams{true, 0, 3}, 0x07), std::string("Green + Black"));            // (a seat of the pair does not play)
        for (uint8_t players = 3; players <= 4; ++players) {                                                // what a room's select shows fits a line of the setup screen's footer (see test_wide_setup)
            for (const StartTeams& c : room_team_choices(players)) ASSERT_TRUE(start_teams_title(c, static_cast<uint8_t>((1u << players) - 1u)).size() <= 40);
        }
    } TEST_END();

    TEST_CASE("N1.24 apply_start_teams: the pairs of the plan become teams with the original's own commands (the first seat invites, the second accepts: the News Flash once for a pair, no invitation stays open), exactly what the same commands by hand make; free for all, a seat that does not play and a team that would be the whole match change nothing; two engines with the same teams stay bit-identical and the teams are part of the hashed state") {
        ants::assets::LevelData lvl;
        ASSERT_TRUE(load_map_file("TREASURE.LVL", lvl));
        const auto fresh = [&](uint8_t roster) {
            auto sim = std::make_unique<SimulationEngine>();
            sim->init(lvl, 7, roster);
            return sim;
        };
        const auto allies = [](const SimulationEngine& sim) {
            std::string out;
            for (uint8_t p = 0; p < MAX_PLAYERS; ++p) out += std::to_string(sim.get_ally_id(p));
            return out;                                                                                     // by seat: the ally's seat, 4 for none
        };
        const auto team_news = [](SimulationEngine& sim) {
            std::vector<std::string> out;
            for (const NewsEvent& n : sim.poll_news_events()) {
                if (n.string_id == strings::kTeamNow) out.push_back(n.message_text);
            }
            return out;
        };
        {   // four seats: two teams, one News Flash for each, nobody left holding an invitation
            auto sim = fresh(0x0F);
            sim->poll_news_events();
            apply_start_teams(*sim, StartTeams{true, 0, 1});
            ASSERT_EQ(allies(*sim), std::string("1032"));
            const std::vector<std::string> news = team_news(*sim);
            ASSERT_EQ(news.size(), size_t{2});
            ASSERT_EQ(news[0], std::string("Green (Green) and Red (Red) are a team now!"));                  // (the proposer first)
            ASSERT_EQ(news[1], std::string("Blue (Blue) and Black (Black) are a team now!"));
            for (uint8_t p = 0; p < MAX_PLAYERS; ++p) ASSERT_EQ(sim->get_world_state().pending_invite_from[p], 255);
            ASSERT_EQ(sim->current_tick(), uint64_t{0});                                                    // before the first tick
            for (int t = 0; t < 200; ++t) sim->tick();
            ASSERT_FALSE(sim->is_match_over());                                                            // two teams of two: the match goes on
            ASSERT_EQ(allies(*sim), std::string("1032"));
        }
        {   // the same by hand: the same state, to the bit
            auto by_hand = fresh(0x0F);
            auto by_call = fresh(0x0F);
            by_hand->apply_command(make_command(CommandType::AllianceInvite, 0, 1));
            by_hand->apply_command(make_command(CommandType::AllianceAccept, 1, 0));
            by_hand->apply_command(make_command(CommandType::AllianceInvite, 2, 3));
            by_hand->apply_command(make_command(CommandType::AllianceAccept, 3, 2));
            apply_start_teams(*by_call, StartTeams{true, 0, 1});
            ASSERT_TRUE(by_hand->state_hash() == by_call->state_hash());
            auto reversed = fresh(0x0F);                                                                    // (1+0: the other seat invites; the teams are the same, the hash is not asked to be)
            apply_start_teams(*reversed, StartTeams{true, 1, 0});
            ASSERT_EQ(allies(*reversed), std::string("1032"));
        }
        {   // names set before the teams are in the News Flash (the order every engine uses: init, the names, the teams)
            auto sim = fresh(0x0F);
            sim->set_player_name(0, "Ann");
            sim->set_player_name(1, "Bob");
            sim->poll_news_events();
            apply_start_teams(*sim, StartTeams{true, 0, 1});
            const std::vector<std::string> news = team_news(*sim);
            ASSERT_EQ(news.size(), size_t{2});
            ASSERT_EQ(news[0], std::string("Ann (Green) and Bob (Red) are a team now!"));
        }
        {   // three seats: the pair is a team and the third seat plays alone
            auto sim = fresh(0x07);
            apply_start_teams(*sim, StartTeams{true, 1, 2});
            ASSERT_EQ(allies(*sim), std::string("4214"));
            ASSERT_EQ(team_news(*sim).size(), size_t{1});
        }
        {   // what cannot be made changes nothing at all: the hash is the plain engine's, no News Flash
            const struct { uint8_t roster; StartTeams teams; } none[] = {
                {0x0F, StartTeams{}}, {0x03, StartTeams{true, 0, 1}}, {0x05, StartTeams{true, 0, 2}}, {0x07, StartTeams{true, 0, 3}}, {0x0D, StartTeams{true, 1, 2}}, {0x0F, StartTeams{true, 2, 2}}, {0x0F, StartTeams{true, 0, 9}}};
            for (const auto& c : none) {
                auto plain = fresh(c.roster);
                auto sim = fresh(c.roster);
                sim->poll_news_events();
                apply_start_teams(*sim, c.teams);
                ASSERT_TRUE(sim->state_hash() == plain->state_hash());
                ASSERT_EQ(team_news(*sim).size(), size_t{0});
                ASSERT_EQ(allies(*sim), std::string("4444"));
            }
        }
        {   // two engines with the same teams and the same commands: the same hash at every tick; the teams are in the hash (a different pair, or none, is a different state from the start)
            auto a = fresh(0x0F);
            auto b = fresh(0x0F);
            auto other_pair = fresh(0x0F);
            auto ffa = fresh(0x0F);
            apply_start_teams(*a, StartTeams{true, 0, 2});
            apply_start_teams(*b, StartTeams{true, 0, 2});
            apply_start_teams(*other_pair, StartTeams{true, 0, 3});
            ASSERT_TRUE(a->state_hash() == b->state_hash());
            ASSERT_TRUE(a->state_hash() != other_pair->state_hash());
            ASSERT_TRUE(a->state_hash() != ffa->state_hash());
            for (int t = 0; t < 300; ++t) {
                a->tick();
                b->tick();
                ASSERT_TRUE(a->state_hash() == b->state_hash());
            }
        }
    } TEST_END();

    TEST_CASE("N1.25 start_teams_for: Which Teams A Start Asks For (The Room's Own, From The Create Block That Made It, For Every Start; A Leader's Request Only When It Is What Starts The Match And Only In A Room That Has None Of Its Own), Over Every Combination") {
        const StartTeams none;
        const StartTeams own_a{true, 0, 1};
        const StartTeams own_b{true, 2, 3};
        const StartTeams asked_a{true, 1, 2};
        const StartTeams asked_b{true, 0, 3};
        const StartTeams impossible{true, 3, 3};                                                           // (no pair: the rule does not judge a pair, plan_start_teams does)
        for (const StartTeams& own : {none, own_a, own_b, impossible}) {
            for (const bool by_leader : {false, true}) {
                for (const StartTeams& asked : {none, asked_a, asked_b, own_a}) {
                    const StartTeams got = start_teams_for(own, by_leader, asked);
                    if (own.set) {
                        ASSERT_TRUE(got == own);                                                           // the room's own win, even over an equal request and over one that starts the match
                    } else if (by_leader) {
                        ASSERT_TRUE(got == asked);                                                         // a room without teams: the leader's request that starts the match
                    } else {
                        ASSERT_FALSE(got.set);                                                             // ... and nothing when the request is not what starts it (the asker left, the room fills up by itself)
                    }
                }
            }
        }
        // the cases that matter by name
        ASSERT_TRUE(start_teams_for(own_a, false, none) == own_a);                                          // a full room's automatic start: no request at all, the room's teams
        ASSERT_TRUE(start_teams_for(own_a, true, asked_a) == own_a);                                        // a leader's START in a room that names others: the room's
        ASSERT_TRUE(start_teams_for(none, true, asked_a) == asked_a);                                       // a room that names none: the leader's
        ASSERT_FALSE(start_teams_for(none, false, asked_a).set);                                            // a request that does not start the match counts for nothing
        ASSERT_FALSE(start_teams_for(none, false, none).set);
        ASSERT_FALSE(start_teams_for(none, true, none).set);
        // a room's teams that the seats cannot make stay the room's: the start is then without teams, never with the request's
        ASSERT_FALSE(plan_start_teams(start_teams_for(StartTeams{true, 0, 3}, true, asked_a), 0x07).why.empty());
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
    run_roster_tests();
    run_drop_tests();
    run_prediction_tests();
    run_start_team_tests();
    std::cout << "\n=======================================================\n Total Test Cases: " << g_test_count << "\n Total Assertions: " << g_assert_count
              << "\n Failed:           " << g_test_failures << "\n=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}
