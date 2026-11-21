// Tests of the rules number 12 of game mode 187 (ants_replay/replay.hpp, docs/REPLAYS.md "Which files play: sim_rules", docs/GAMEPLAY.md "187"): nothing is put on the ground, no power-up from an ant of a special type
// that dies or that takes another. The numbers 6, 7, 10 and 11 (187) keep the drops of the original's game, and every number belongs to one mode. This build plays 5, 8, 9 and 13 for the original's game and 6, 7, 10, 11 and 12 for 187.
//   RP16.1  the numbers: what a build plays, what a head may say, what a match is recorded under, what a refusal says
//   RP16.2  the recordings of the old numbers (11, 10, 7 and 6), made in code on the match of no_drops_scenario.hpp, play to the hashes that the build before the number made of them, with the power-ups on the ground
//   RP16.3  the same match recorded under 12 leaves nothing on the ground at any turn and is another match; each file is another match under the other number
//   RP16.4  a copy of the engine in the middle of the match keeps the rules of its number
//   RP16.5  the player of a recording sets the rule of its number up (begin_match): off for 6, 7, 10 and 11, on for 12 and for the original's game, which never reads it
#include "ants_replay/player.hpp"
#include "ants_replay/recorder.hpp"
#include "ants_replay/replay.hpp"
#include "ants_sim/sim_engine.hpp"
#include "no_drops_scenario.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>
#include "../common/test_assert.hpp"

using namespace ants;
using namespace ants::replay;

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

namespace {

namespace scenario = ants::no_drops_scenario;
constexpr uint8_t k187 = static_cast<uint8_t>(sim::GameMode::Kills187);

// What the build before the number 12 made of the match of no_drops_scenario.hpp under the numbers 11, 10, 7 and 6: the turns it ran, the engine's state hash at the end, and a fold of every hash the recording holds
// (FNV-1a over the 32-bit hashes, one a second of play). Taken by playing the same header against that build (the commit 010c2c90, the last before the number 12), not against this one.
struct Pinned {
    uint32_t turns;
    uint64_t final_hash;
    size_t hashes;
    uint64_t fold;
};
// 11, 10 and 7 play the match the same way (the scenario has no fire wall and no team that only holds eggs): two Combat ants die, the walkers take their power-ups and leave their own
constexpr Pinned kBrawl11 = {2405, 0xe46b155ef5b2624dull, 24, 0x78df07c56d0c5600ull};
constexpr Pinned kBrawl10 = {2405, 0xe46b155ef5b2624dull, 24, 0x78df07c56d0c5600ull};
constexpr Pinned kBrawl7 = {2405, 0xe46b155ef5b2624dull, 24, 0x78df07c56d0c5600ull};
// 6 plays the hill that works, so the match is another one (nobody dies)
constexpr Pinned kBrawl6 = {2405, 0x95c58e71533855edull, 24, 0x81d79b2faaffcc07ull};
// What this build makes of the same match under the number 12
constexpr Pinned kBrawl12 = {2405, 0xaca6c206b82307d5ull, 24, 0xeeb8f41dfe8094b0ull};

struct Made {
    std::vector<uint8_t> file;
    Replay replay;
    uint32_t turns{0};
    uint64_t hash{0};
    bool over{false};
    uint32_t lost{0};                            // ants that the two teams lost
    size_t most_on_ground{0};                    // the most power-ups on the ground at the end of any turn
    uint32_t turns_with_one{0};                  // turns at the end of which there was at least one
    std::vector<uint8_t> left_on_ground;         // the types of the power-ups on the ground at the end
    std::string error;
};

uint64_t fold_of(const std::vector<uint32_t>& hashes) {
    uint64_t fold = 1469598103934665603ull;
    for (const uint32_t h : hashes) fold = (fold ^ h) * 1099511628211ull;
    return fold;
}

std::vector<uint8_t> power_ups_on_ground(const sim::SimulationEngine& engine) {
    std::vector<uint8_t> types;
    const sim::Grid& grid = engine.grid();
    for (uint32_t y = 0; y < grid.height(); ++y) {
        for (uint32_t x = 0; x < grid.width(); ++x) {
            const sim::TileCoord t{static_cast<int32_t>(x), static_cast<int32_t>(y)};
            if (grid.has_powerup_at(t)) types.push_back(grid.get_powerup_type(t));
        }
    }
    return types;
}

Header scenario_head(uint16_t rules) {
    Header head;
    head.game_version = "v0.0.0";
    head.build_id = "test";
    head.venue = "local game";
    head.map_name = "SYNTH.LVL";
    head.seed = scenario::kSeed;
    head.roster = scenario::kRoster;
    head.set_mode(k187);
    head.sim_rules = rules;
    return head;
}

// One turn of the match of the scenario on `engine`: the orders that the scenario makes from its state, the tick, and the audio and news that nobody listens to
void step(sim::SimulationEngine& engine, uint32_t turn, Recorder* rec) {
    for (const sim::Command& c : scenario::orders(engine, turn)) {
        engine.apply_command(c);
        if (rec != nullptr) rec->on_command(c);
    }
    engine.tick();
    engine.clear_news_events();
    engine.clear_audio_events();
    if (rec != nullptr) rec->on_tick(engine);
}

// The match of the scenario, set up as the player of a recording of the number `rules` sets it up (begin_match), played and recorded as a game on one machine does it. A file is stamped with its number after the play
// (a Recorder writes the number of now for a mode that is not 0), which is what a build of that number wrote.
Made make(uint16_t rules, const assets::LevelData& level) {
    Made m;
    const Header head = scenario_head(rules);
    Replay shell;
    shell.head = head;
    sim::SimulationEngine engine;
    begin_match(engine, shell, level, false);
    Recorder rec(head);
    uint32_t turn = 0;
    for (; turn < 4000 && !engine.is_match_over(); ++turn) {
        step(engine, turn, &rec);
        const size_t on_ground = power_ups_on_ground(engine).size();
        m.most_on_ground = std::max(m.most_on_ground, on_ground);
        if (on_ground > 0) ++m.turns_with_one;
    }
    m.turns = turn;
    m.hash = engine.state_hash().total;
    m.over = engine.is_match_over();
    m.left_on_ground = power_ups_on_ground(engine);
    std::sort(m.left_on_ground.begin(), m.left_on_ground.end());
    for (uint8_t seat = 0; seat < 2; ++seat) m.lost += engine.get_player_stats(seat).friendly_lost;
    m.file = rec.finish(engine, m.error);
    if (!m.file.empty()) {
        std::string why;
        if (!decode(m.file.data(), m.file.size(), m.replay, why)) m.error = why;
        if (m.replay.head.sim_rules != rules) {
            m.replay.head.sim_rules = rules;                                                 // (a match of the number 11, 10, 7 or 6: the Recorder wrote 12)
            m.file = encode(m.replay, why);
            if (m.file.empty()) m.error = why;
        }
    }
    return m;
}

Replay with_rules(Replay r, uint16_t rules) {
    r.head.sim_rules = rules;
    return r;
}

bool plays_to(const Made& m, const Pinned& pinned, const assets::LevelData& level, uint16_t rules) {
    if (m.turns != pinned.turns || m.hash != pinned.final_hash || m.replay.hashes.size() != pinned.hashes || fold_of(m.replay.hashes) != pinned.fold) {
        std::cout << "\n    rules " << rules << ": the match ran " << m.turns << " turns and ends in 0x" << std::hex << m.hash << "ull, " << std::dec << m.replay.hashes.size() << " hashes, fold 0x" << std::hex << fold_of(m.replay.hashes)
                  << "ull" << std::dec << "\n";
    }
    bool ok = m.error.empty() && !m.file.empty() && m.replay.complete && m.replay.head.sim_rules == rules && m.replay.head.mode == k187 && plays_here(m.replay.head);
    ok = ok && m.turns == pinned.turns && m.hash == pinned.final_hash && m.replay.total_turns == pinned.turns && m.replay.final_hash == pinned.final_hash;
    ok = ok && m.replay.hashes.size() == pinned.hashes && fold_of(m.replay.hashes) == pinned.fold;
    const Outcome o = play(m.replay, level);
    return ok && o.ran && o.ok && o.complete && o.hash == pinned.final_hash && o.turns == pinned.turns && o.first_bad_turn == 0u;
}

}  // namespace

int main() {
    std::cout << "=== Rules number 12 of game mode 187 (11, 10, 7 and 6 keep the drops of the original's game) ===\n";
    const assets::LevelData level = scenario::level();

    TEST_CASE("RP16.1 This build plays 5, 8, 9 and 13 for the original's game and 6, 7, 10, 11 and 12 for 187, nothing else; a 187 match is recorded under 12; every number names its one mode; a refusal names all nine") {
        ASSERT_TRUE(kSimRulesMode187 == 15 && kSimRulesMode187BeforeCantGo == 12 && kSimRulesMode187BeforeNoDrops == 11 && kSimRulesMode187BeforeEndFix == 10 && kSimRulesMode187BeforeFireWall == 7 && kSimRulesMode187Heal == 6);
        ASSERT_TRUE(kSimRules == 14 && kSimRulesBeforeCantGo == 13 && kSimRulesBeforeEmptyRecord == 9 && kSimRulesBeforeFireWall == 8 && kSimRulesBeforeLastSide == 5 && kSimRulesNewest == 15);
        ASSERT_TRUE(sim_rules_for_mode(0) == 14 && sim_rules_for_mode(1) == 15 && sim_rules_for_mode(2) == 0 && sim_rules_for_mode(255) == 0);
        for (uint16_t rules = 0; rules < 17; ++rules) {
            ASSERT_EQ(plays_mode_rules(0, rules), rules == 5 || rules == 8 || rules == 9 || rules == 13 || rules == 14);
            ASSERT_EQ(plays_mode_rules(k187, rules), rules == 6 || rules == 7 || rules == 10 || rules == 11 || rules == 12 || rules == 15);
            ASSERT_FALSE(plays_mode_rules(2, rules));
            ASSERT_FALSE(plays_mode_rules(255, rules));
            ASSERT_EQ(fire_wall_as_original_rules(rules), rules >= 9);                       // (10, 11 and 12 have the loader of the original, as 9 does)
        }
        ASSERT_EQ(played_rules_text(), std::string("5, 6, 7, 8, 9, 10, 11, 12, 13, 14 and 15"));
        Header head;
        head.map_name = "TINY.LVL";
        head.roster = 0x0F;
        for (uint16_t rules = 0; rules < 18; ++rules) {
            for (const uint8_t mode : {uint8_t{0}, k187}) {
                Header h = head;
                h.set_mode(mode);
                h.sim_rules = rules;
                Replay r;
                r.head = h;
                std::string error;
                const std::vector<uint8_t> bytes = encode(r, error);
                const bool mode_has_it = mode == 0 ? (rules == 5 || rules == 8 || rules == 9 || rules == 13 || rules == 14) : (rules == 6 || rules == 7 || rules == 10 || rules == 11 || rules == 12 || rules == 15);
                const bool other_mode_has_it = mode == 0 ? (rules == 6 || rules == 7 || rules == 10 || rules == 11 || rules == 12 || rules == 15) : (rules == 5 || rules == 8 || rules == 9 || rules == 13 || rules == 14);
                ASSERT_EQ(plays_here(h), mode_has_it);
                ASSERT_EQ(!bytes.empty(), !other_mode_has_it && (mode == 0 || rules >= 6));      // (a number of another mode is no file; 4 and 13 are files, only not played)
                if (!bytes.empty()) {
                    Replay back;
                    ASSERT_TRUE(decode(bytes.data(), bytes.size(), back, error) && back.head.sim_rules == rules && back.head.mode == mode && plays_here(back.head) == mode_has_it);
                }
            }
        }
        for (const uint16_t said : {uint16_t{5}, uint16_t{6}, uint16_t{7}, uint16_t{8}, uint16_t{9}, uint16_t{10}, uint16_t{11}, uint16_t{12}, uint16_t{15}}) {
            Header h = head;
            h.set_mode(k187);
            h.sim_rules = said;
            Recorder rec(h);
            ASSERT_EQ(rec.replay().head.sim_rules, 15);                                      // (a mode that is not 0 gets the number of that mode whatever the head said)
        }
        Recorder fresh(head);
        ASSERT_TRUE(fresh.replay().head.sim_rules == 14 && fresh.replay().head.mode == 0);
        Header foreign = head;
        foreign.sim_rules = 16;
        ASSERT_TRUE(rules_refusal(foreign).find("simulation rules 16; this build plays simulation rules 5, 6, 7, 8, 9, 10, 11, 12, 13, 14 and 15") != std::string::npos);
    } TEST_END();

    TEST_CASE("RP16.2 A recording of the number 11, 10, 7 or 6, made in code on the match of the scenario, plays to every hash it holds and to the final hash that the build before the number made of it; under 11, 10 and 7 the dead leave power-ups") {
        ASSERT_TRUE(plays_to(make(11, level), kBrawl11, level, 11));
        ASSERT_TRUE(plays_to(make(10, level), kBrawl10, level, 10));
        ASSERT_TRUE(plays_to(make(7, level), kBrawl7, level, 7));
        ASSERT_TRUE(plays_to(make(6, level), kBrawl6, level, 6));
        // the old matches really are the ones that the rule changes: two Combat ants die, their power-ups lie on the ground, two ants that are not Combat ants take them and leave a power-up of their own each (a Bomber's and a Fire's)
        for (const uint16_t rules : {uint16_t{11}, uint16_t{10}, uint16_t{7}}) {
            const Made old = make(rules, level);
            ASSERT_TRUE(old.error.empty() && old.lost == 2 && old.most_on_ground == 2 && old.turns_with_one > 100);
            ASSERT_TRUE(old.left_on_ground == (std::vector<uint8_t>{1, 2}));                // (the Bomber's power-up is 1, the Fire's 2)
            std::string error;
            ASSERT_EQ(encode(old.replay, error), old.file);                                  // written again, the file is the same bytes
        }
        const Made heal = make(6, level);
        ASSERT_TRUE(heal.error.empty() && heal.most_on_ground == 0);                         // (the hill that works plays another match, with nobody dead: its pin is its own)
    } TEST_END();

    TEST_CASE("RP16.3 The same match recorded under the number 12 leaves nothing on the ground at any turn, ants still die, and it is another match than under 11; each file is another match under the other number") {
        const Made now = make(12, level);
        ASSERT_TRUE(now.error.empty() && now.over && now.lost >= 2);
        ASSERT_TRUE(now.most_on_ground == 0 && now.turns_with_one == 0 && now.left_on_ground.empty());
        ASSERT_TRUE(plays_to(now, kBrawl12, level, 12));
        ASSERT_TRUE(now.hash != kBrawl11.final_hash);
        // the commands are made from the state of each engine: without a power-up on the ground there is nobody to send to one, so the file has fewer
        const Made old = make(11, level);
        ASSERT_TRUE(now.replay.commands.size() < old.replay.commands.size());
        // a file played under another number of its mode is another match
        const Outcome old_as_new = play(with_rules(old.replay, 12), level);
        ASSERT_TRUE(!old_as_new.ok && old_as_new.first_bad_turn > 0u);                       // (the power-up that the file's orders walk to is not there under 12)
        const Outcome new_as_old = play(with_rules(now.replay, 11), level);
        ASSERT_TRUE(!new_as_old.ok && new_as_old.first_bad_turn > 0u);
        // a number nobody plays is not played
        ASSERT_FALSE(play(with_rules(now.replay, 14), level).ran);
        ASSERT_FALSE(play(with_rules(now.replay, 9), level).ran);
    } TEST_END();

    TEST_CASE("RP16.4 A copy of the engine in the middle of the match goes on with the rules of its number: under 12 nothing lands on the ground, under 11 the same turns leave power-ups, and both end where the engine that was copied ends (a client's predicted engine is such a copy)") {
        for (const uint16_t rules : {kSimRulesMode187BeforeCantGo, kSimRulesMode187BeforeNoDrops}) {
            Replay shell;
            shell.head = scenario_head(rules);
            sim::SimulationEngine engine;
            begin_match(engine, shell, level, false);
            ASSERT_EQ(engine.no_drops_187(), rules == kSimRulesMode187BeforeCantGo);
            uint32_t turn = 0;
            for (; turn < 1000; ++turn) step(engine, turn, nullptr);                         // (before the first death: a power-up has not been dropped yet under either number)
            ASSERT_EQ(power_ups_on_ground(engine).size(), 0u);
            sim::SimulationEngine copy(engine);
            ASSERT_EQ(copy.no_drops_187(), rules == kSimRulesMode187BeforeCantGo);
            for (uint32_t t = turn; t < 4000 && (!engine.is_match_over() || !copy.is_match_over()); ++t) {
                if (!engine.is_match_over()) step(engine, t, nullptr);
                if (!copy.is_match_over()) step(copy, t, nullptr);
            }
            ASSERT_TRUE(engine.is_match_over() && copy.is_match_over());
            ASSERT_EQ(copy.state_hash().total, engine.state_hash().total);
            ASSERT_EQ(power_ups_on_ground(copy), power_ups_on_ground(engine));
            ASSERT_EQ(power_ups_on_ground(copy).size(), rules == kSimRulesMode187BeforeCantGo ? 0u : 2u);
        }
    } TEST_END();

    TEST_CASE("RP16.5 The player of a recording sets the rule of its number up before the first tick: off for 6, 7, 10 and 11 (a Combat ant that dies leaves its power-up), on for 12 (it leaves none); the original's game never reads it") {
        for (const uint16_t rules : {uint16_t{6}, uint16_t{7}, uint16_t{10}, uint16_t{11}, uint16_t{12}, uint16_t{15}}) {
            Replay shell;
            shell.head = scenario_head(rules);
            sim::SimulationEngine engine;
            begin_match(engine, shell, level, false);
            ASSERT_EQ(engine.no_drops_187(), rules >= kSimRulesMode187BeforeCantGo);
            ASSERT_TRUE(engine.get_unit(1).type == sim::AntType::Combat);                     // (the first ant of the scenario is a Combat ant of seat 0)
            engine.kill_unit(1);
            ASSERT_EQ(power_ups_on_ground(engine).size(), rules >= kSimRulesMode187BeforeCantGo ? 0u : 1u);
        }
        for (const uint16_t rules : {kSimRules, kSimRulesBeforeFireWall, kSimRulesBeforeLastSide}) {
            Replay shell;
            shell.head = scenario_head(rules);
            shell.head.set_mode(0);                                                              // (the same map as the original's game: its ants begin as the markers say too)
            shell.head.sim_rules = rules;
            sim::SimulationEngine engine;
            begin_match(engine, shell, level, false);
            ASSERT_TRUE(engine.no_drops_187() && engine.game_mode() == sim::GameMode::HighestScore);
            engine.kill_unit(1);
            ASSERT_EQ(power_ups_on_ground(engine).size(), 1u);                                   // (the original's game drops, whatever the switch says)
        }
    } TEST_END();

    std::cout << "\n=======================================================\n"
              << " TEST SUMMARY: Total test cases: " << g_test_count << ", Total assertions: " << g_assert_count << ", Failed: " << g_test_failures << "\n"
              << "=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}
