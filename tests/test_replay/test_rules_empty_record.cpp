// Tests of the rules number 13 of the original's game (ants_replay/replay.hpp, docs/REPLAYS.md "Which files play: sim_rules", docs/GAME_REVERSE_ENGINEERING.md 5.40, "A lunchbox on the anchor of an emptied record"): from 13 a bite takes
// its unit from the first object of its food tile that still has one, so a lunchbox that falls on the anchor of an emptied pile is eaten once; the original takes the first object whatever it holds, and a recording
// of 9 (or 8 or 5) plays so. This build plays 5, 8, 9 and 13 for the original's game, 187 keeps 6, 7, 10, 11 and 12 (it has no food on the ground), and every number belongs to one mode.
//   RP17.1  the numbers: what a build plays, which of them have the rule, what a head may say, what a match is recorded under, what a refusal says
//   RP17.2  a recording under the number 9 on the match of empty_record_scenario.hpp (the same orders, an emptied object behind which a second one stands) plays to every hash and to the final hash that the build before the
//           rule made of it, with every Worker that is sent to the tile eating from it, over and over
//   RP17.3  the same match recorded under 13 feeds the Workers only as many times as the two objects hold units, and is another match; each file is another match under the other number
//   RP17.4  the engine copy and the rule: a copy of the engine in the middle of the match keeps the rule of its number and plays on to the same hash
#include "ants_replay/player.hpp"
#include "ants_replay/recorder.hpp"
#include "ants_replay/replay.hpp"
#include "ants_sim/sim_engine.hpp"
#include "empty_record_scenario.hpp"

#include <cstdint>
#include <functional>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>
#include "../common/test_assert.hpp"

using namespace ants;
using namespace ants::replay;
namespace scenario = ants::empty_record_scenario;

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

constexpr uint8_t k187 = static_cast<uint8_t>(sim::GameMode::Kills187);

// What the build before the rule made of the match of empty_record_scenario.hpp (it ends on the clock, two minutes and the rest of the last second): the turns it ran, the engine's state hash at the end, the low 32 bits
// of the hash after turns 100, 400 and 1200, and how many times the Workers of seat 1 got food ("Got Food!"). Taken by playing the same header against that build (the commit that this one grew from), not by this one.
constexpr uint32_t kBeforeTurns = 2405;
constexpr uint64_t kBeforeFinalHash = 0x11404912d384fd5cull;
constexpr uint32_t kBeforeHash100 = 0xc5a8a515;
constexpr uint32_t kBeforeHash400 = 0xc6f08465;
constexpr uint32_t kBeforeHash1200 = 0x3ac36b3a;
constexpr int kBeforeBites = 23;

struct Made {
    std::vector<uint8_t> file;
    Replay replay;
    uint32_t turns{0};
    uint64_t hash{0};
    int bites{0};                                              // the times that a Worker of seat 1 got food
    std::string error;
};

Header scenario_head(uint16_t rules) {
    Header head;
    head.game_version = "v0.0.0";
    head.build_id = "test";
    head.venue = "local game";
    head.map_name = "SYNTH.LVL";
    head.seed = scenario::kSeed;
    head.roster = scenario::kRoster;
    head.set_mode(0);
    head.sim_rules = rules;
    return head;
}

// The match of the scenario, played and recorded as a game on one machine does it, by the rules of the number `rules`
Made make(uint16_t rules) {
    Made m;
    const assets::LevelData level = scenario::level();
    sim::SimulationEngine engine;
    engine.set_game_mode(sim::GameMode::HighestScore);
    engine.set_bite_skips_empty_record(bite_skips_empty_record_rules(rules));
    engine.set_path_answers_after_commands(path_answers_after_commands_rules(rules));
    engine.init(level, scenario::kSeed, scenario::kRoster);
    Recorder rec(scenario_head(rules));
    uint32_t turn = 0;
    for (; turn < 4000 && !engine.is_match_over(); ++turn) {
        for (const sim::Command& c : scenario::orders(engine, turn)) {
            engine.apply_command(c);
            rec.on_command(c);
        }
        engine.tick();
        if (engine.has_news_event(1, scenario::kNewsGotFood)) ++m.bites;
        engine.clear_news_events();
        engine.clear_audio_events();
        rec.on_tick(engine);
    }
    m.turns = turn;
    m.hash = engine.state_hash().total;
    m.file = rec.finish(engine, m.error);
    if (!m.file.empty()) {
        std::string why;
        if (!decode(m.file.data(), m.file.size(), m.replay, why)) m.error = why;
    }
    return m;
}

Replay with_rules(Replay r, uint16_t rules) {
    r.head.sim_rules = rules;
    return r;
}

// The times that a Worker of seat 1 got food while `replay` plays (the player's own hook: the news of the tick is still queued when it runs)
int bites_while_playing(const Replay& replay, const assets::LevelData& level, Outcome& outcome) {
    int bites = 0;
    Hooks hooks;
    hooks.after_tick = [&](uint32_t, sim::SimulationEngine& engine) {
        if (engine.has_news_event(1, scenario::kNewsGotFood)) ++bites;
    };
    outcome = play(replay, level, hooks);
    return bites;
}

}  // namespace

int main() {
    std::cout << "=== Rules number 13 of the original's game: the empty record ===\n";
    const assets::LevelData level = scenario::level();

    TEST_CASE("RP17.1 The original's game is recorded under 13 and played under 5, 8, 9 and 13; only 13 has the rule of the empty record; 187 keeps its numbers; a refusal names all nine") {
        ASSERT_TRUE(kSimRules == 14 && kSimRulesBeforeCantGo == 13 && kSimRulesBeforeEmptyRecord == 9 && kSimRulesEmptyRecordFirst == 13 && kSimRulesBeforeFireWall == 8 && kSimRulesBeforeLastSide == 5 && kSimRulesMode187 == 15 && kSimRulesMode187BeforeCantGo == 12 && kSimRulesNewest == 15);
        ASSERT_TRUE(sim_rules_for_mode(0) == 14 && sim_rules_for_mode(1) == 15);
        for (uint16_t rules = 0; rules < 18; ++rules) {
            ASSERT_EQ(plays_mode_rules(0, rules), rules == 5 || rules == 8 || rules == 9 || rules == 13 || rules == 14);
            ASSERT_EQ(plays_mode_rules(k187, rules), rules == 6 || rules == 7 || rules == 10 || rules == 11 || rules == 12 || rules == 15);
            ASSERT_EQ(bite_skips_empty_record_rules(rules), rules >= 13);                    // (187's 12 has no food on the ground: it plays the same with the original's lookup)
        }
        ASSERT_EQ(played_rules_text(), std::string("5, 6, 7, 8, 9, 10, 11, 12, 13, 14 and 15"));
        Header head;
        head.map_name = "TINY.LVL";
        head.roster = 0x0F;
        ASSERT_EQ(head.sim_rules, 14);                                                       // (a head that nobody changed is a head of the original's game now)
        for (const uint16_t rules : {uint16_t{5}, uint16_t{8}, uint16_t{9}, uint16_t{13}, uint16_t{14}}) {
            Header h = head;
            h.sim_rules = rules;
            Replay r;
            r.head = h;
            std::string error;
            const std::vector<uint8_t> bytes = encode(r, error);
            ASSERT_FALSE(bytes.empty());
            ASSERT_TRUE(plays_here(h));
            Replay back;
            ASSERT_TRUE(decode(bytes.data(), bytes.size(), back, error) && back.head.sim_rules == rules && back.head.mode == 0);
        }
        {
            Header h = head;                                                                 // (187 under the original's new number, and the original's under 187's: no file)
            h.set_mode(k187);
            h.sim_rules = 13;
            Replay r;
            r.head = h;
            std::string error;
            ASSERT_TRUE(encode(r, error).empty() && !plays_here(h));
            h.set_mode(0);
            h.sim_rules = 12;
            r.head = h;
            ASSERT_TRUE(encode(r, error).empty() && !plays_here(h));
            h.set_mode(k187);
            h.sim_rules = 9;
            r.head = h;
            ASSERT_TRUE(encode(r, error).empty() && !plays_here(h));
        }
        Header plain = head;
        Recorder fresh(plain);
        ASSERT_TRUE(fresh.replay().head.sim_rules == 14 && fresh.replay().head.mode == 0);
        plain.sim_rules = 9;
        Recorder old(plain);
        ASSERT_EQ(old.replay().head.sim_rules, 9);                                           // (a head that says 9 is recorded as 9: the engine that plays it is set up for it)
        Header foreign = head;
        foreign.sim_rules = 16;
        ASSERT_FALSE(plays_here(foreign));
        ASSERT_TRUE(rules_refusal(foreign).find("simulation rules 5, 6, 7, 8, 9, 10, 11, 12, 13, 14 and 15") != std::string::npos);
        // the player of a recording sets the engine's rule up from the head's number
        for (const uint16_t rules : {uint16_t{5}, uint16_t{8}, uint16_t{9}, uint16_t{13}, uint16_t{14}}) {
            sim::SimulationEngine engine;
            Replay r;
            r.head = scenario_head(rules);
            begin_match(engine, r, level, false);
            ASSERT_EQ(engine.bite_skips_empty_record(), rules >= 13);
        }
        {
            sim::SimulationEngine engine;
            Replay r;
            r.head = scenario_head(12);
            r.head.set_mode(k187);
            r.head.sim_rules = 12;                                                           // (set_mode puts the number of the mode now)
            begin_match(engine, r, level, false);
            ASSERT_FALSE(engine.bite_skips_empty_record());                                  // (a 187 recording of 12 plays as the build before made of it)
        }
    } TEST_END();

    TEST_CASE("RP17.2 A recording under the number 9 on the empty-record match plays to every hash it holds and to the final hash that the build before the rule made of it; every Worker that is sent eats from the tile") {
        const Made nine = make(kSimRulesBeforeEmptyRecord);
        ASSERT_TRUE(nine.error.empty() && !nine.file.empty());
        ASSERT_TRUE(nine.replay.complete && nine.replay.head.sim_rules == 9 && nine.replay.head.mode == 0 && plays_here(nine.replay.head));
        ASSERT_EQ(nine.turns, kBeforeTurns);
        ASSERT_EQ(nine.hash, kBeforeFinalHash);                                              // (what the build before the rule made of the very same orders)
        ASSERT_EQ(nine.replay.total_turns, kBeforeTurns);
        ASSERT_EQ(nine.replay.final_hash, kBeforeFinalHash);
        ASSERT_TRUE(nine.replay.hashes.size() == 24 && nine.replay.hashes[0] == kBeforeHash100 && nine.replay.hashes[3] == kBeforeHash400 && nine.replay.hashes[11] == kBeforeHash1200);
        ASSERT_EQ(nine.bites, kBeforeBites);                                                 // (the Workers go back to the pile that they remember after each deposit, and eat again: the one unit of the emptied object is never missed)
        ASSERT_TRUE(nine.bites > static_cast<int>(scenario::kUnitsFirst + scenario::kUnitsSecond) * 3);
        Outcome o;
        ASSERT_EQ(bites_while_playing(nine.replay, level, o), kBeforeBites);
        ASSERT_TRUE(o.ran && o.ok && o.complete && o.match_over);
        ASSERT_EQ(o.turns, kBeforeTurns);
        ASSERT_EQ(o.hash, kBeforeFinalHash);
        ASSERT_EQ(o.hashes_checked, 24u);
        ASSERT_EQ(o.first_bad_turn, 0u);
        std::string error;
        ASSERT_EQ(encode(nine.replay, error), nine.file);                                    // (written again, the file is the same bytes)
        // the files of 8 and of 5 play the same match to the same hash: they differ from 9 by the end rule and the loader, and this match has no fire wall and is ended by the clock
        for (const uint16_t older : {kSimRulesBeforeFireWall, kSimRulesBeforeLastSide}) {
            Outcome other;
            ASSERT_EQ(bites_while_playing(with_rules(nine.replay, older), level, other), kBeforeBites);
            ASSERT_TRUE(other.ran && other.ok);
            ASSERT_EQ(other.hash, kBeforeFinalHash);
        }
    } TEST_END();

    TEST_CASE("RP17.3 The same match recorded under 13 feeds the Workers only as many times as the objects hold units, and is another match; each file is another match under the other number") {
        const Made nine = make(kSimRulesBeforeEmptyRecord);
        const Made thirteen = make(kSimRulesBeforeCantGo);
        ASSERT_TRUE(thirteen.error.empty() && !thirteen.file.empty() && nine.error.empty());
        ASSERT_TRUE(thirteen.replay.complete && thirteen.replay.head.sim_rules == 13 && plays_here(thirteen.replay.head));
        ASSERT_EQ(thirteen.bites, scenario::kUnitsFirst + scenario::kUnitsSecond);           // (the first object's unit, then the three of the second; the last two Workers find the tile empty)
        ASSERT_TRUE(thirteen.hash != nine.hash);
        Outcome o;
        ASSERT_EQ(bites_while_playing(thirteen.replay, level, o), scenario::kUnitsFirst + scenario::kUnitsSecond);
        ASSERT_TRUE(o.ran && o.ok && o.complete);
        ASSERT_EQ(o.turns, thirteen.turns);
        ASSERT_EQ(o.hash, thirteen.hash);
        ASSERT_EQ(o.first_bad_turn, 0u);
        // the two files hold the same commands (the orders come at fixed turns)
        ASSERT_EQ(thirteen.replay.commands.size(), nine.replay.commands.size());
        for (size_t i = 0; i < nine.replay.commands.size(); ++i) {
            ASSERT_EQ(thirteen.replay.commands[i].turn, nine.replay.commands[i].turn);
            ASSERT_TRUE(thirteen.replay.commands[i].command.ants == nine.replay.commands[i].command.ants);
        }
        // the file of 9 under the number 13 is another match, and the file of 13 under 9 as well
        const Outcome nine_as_thirteen = play(with_rules(nine.replay, 13), level);
        ASSERT_FALSE(nine_as_thirteen.ok);
        ASSERT_TRUE(nine_as_thirteen.first_bad_turn > 0);
        const Outcome thirteen_as_nine = play(with_rules(thirteen.replay, 9), level);
        ASSERT_FALSE(thirteen_as_nine.ok);
        // and a number nobody plays is not played at all
        ASSERT_FALSE(play(with_rules(thirteen.replay, 16), level).ran);
        ASSERT_FALSE(play(with_rules(thirteen.replay, 12), level).ran);
    } TEST_END();

    TEST_CASE("RP17.4 A copy of the engine in the middle of the match keeps the rule of its number and plays on to the same hash, under 13 and under 9 (a client's predicted engine is such a copy)") {
        for (const uint16_t rules : {kSimRulesBeforeCantGo, kSimRulesBeforeEmptyRecord}) {
            sim::SimulationEngine engine;
            engine.set_game_mode(sim::GameMode::HighestScore);
            engine.set_bite_skips_empty_record(bite_skips_empty_record_rules(rules));
            engine.set_path_answers_after_commands(path_answers_after_commands_rules(rules));
            engine.init(level, scenario::kSeed, scenario::kRoster);
            uint32_t turn = 0;
            const auto step = [&](sim::SimulationEngine& e) {
                for (const sim::Command& c : scenario::orders(e, turn)) e.apply_command(c);
                e.tick();
                e.clear_news_events();
                e.clear_audio_events();
            };
            for (; turn < 500; ++turn) step(engine);
            sim::SimulationEngine copy(engine);
            for (; turn < 1500; ++turn) {
                step(engine);
                step(copy);
                ASSERT_TRUE(engine.state_hash() == copy.state_hash());
            }
            ASSERT_EQ(copy.bite_skips_empty_record(), rules == kSimRulesBeforeCantGo);
            ASSERT_EQ(engine.bite_skips_empty_record(), copy.bite_skips_empty_record());
        }
    } TEST_END();

    std::cout << "\n=======================================================\n"
              << " TEST SUMMARY: Total test cases: " << g_test_count << ", Total assertions: " << g_assert_count << ", Failed: " << g_test_failures << "\n"
              << "=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}
