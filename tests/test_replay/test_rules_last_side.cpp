// Tests of the rules numbers of the original's game (ants_replay/replay.hpp, docs/REPLAYS.md "Which files play: sim_rules", docs/GAMEPLAY.md "The last side standing"): the original's game was 5 (CHECKGO alone) and is
// 8 since a match also ends when one side is left and no food can score (9 since the loader's fire wall, test_rules_fire_wall, has that rule too). This build plays 5, 8 and 9, 187 plays 6, 7, 10 and 11, and every number belongs to one mode.
//   RP13.1  the numbers: what a build plays, what a head may say, what a match is recorded under, what a refusal says
//   RP13.2  a recording under the number 5 that hits the state (one side left, no food) and plays on to the clock plays to every hash and to the final hash that the build before the rule made of it
//   RP13.3  the same match recorded under the number 8 ends where the rule ends it, with the survivor first; each file is another match under the other number
//   RP13.4  the engine copy and the rule: a copy of the engine in the middle of the match ends at the same turn with the same hash
#include "ants_replay/player.hpp"
#include "ants_replay/recorder.hpp"
#include "ants_replay/replay.hpp"
#include "ants_sim/sim_engine.hpp"
#include "last_side_scenario.hpp"

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

constexpr uint8_t k187 = static_cast<uint8_t>(sim::GameMode::Kills187);

// What the build before the last-side rule made of the match of last_side_scenario.hpp (every machine plays it to the clock): the turns it ran, the engine's state hash at the end, and the low 32 bits of the hash
// after turns 100, 400 and 1200. Taken by playing the same code against that build (the commit that this one grew from), not by this one.
constexpr uint32_t kBeforeTurns = 1205;
constexpr uint64_t kBeforeFinalHash = 0x4a75b4582a7c0d06ull;
constexpr uint32_t kBeforeHash100 = 0x117b340c;
constexpr uint32_t kBeforeHash400 = 0x2e145732;
constexpr uint32_t kBeforeHash1200 = 0xf5294859;

struct Made {
    std::vector<uint8_t> file;
    Replay replay;
    uint32_t turns{0};
    uint64_t hash{0};
    bool over{false};
    sim::MatchResult result;
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

// The match of the scenario, played and recorded as a game on one machine does it, by the rules of the number `rules` (5: the original's end rule alone; 8: the last side standing)
Made make(uint16_t rules) {
    Made m;
    const assets::LevelData level = scenario::level();
    sim::SimulationEngine engine;
    engine.set_game_mode(sim::GameMode::HighestScore);
    engine.set_last_side_rule(rules != kSimRulesBeforeLastSide);
    engine.init(level, scenario::kSeed, scenario::kRoster);
    Recorder rec(scenario_head(rules));
    uint32_t turn = 0;
    for (; turn < 4000 && !engine.is_match_over(); ++turn) {
        for (const sim::Command& c : scenario::orders(engine, turn)) {
            engine.apply_command(c);
            rec.on_command(c);
        }
        engine.tick();
        engine.clear_news_events();
        engine.clear_audio_events();
        rec.on_tick(engine);
    }
    m.turns = turn;
    m.hash = engine.state_hash().total;
    m.over = engine.is_match_over();
    m.result = engine.get_world_state().match_result;
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

}  // namespace

int main() {
    std::cout << "=== Rules numbers of the original's game: 5 and 8 ===\n";
    const assets::LevelData level = scenario::level();

    TEST_CASE("RP13.1 This build plays 5, 8 and 9 for the original's game and 6, 7, 10 and 11 for 187, nothing else; a match of the original's game is recorded under 9, a 187 match under 11; every number names its one mode; a refusal names all seven") {
        ASSERT_TRUE(kSimRules == 9 && kSimRulesBeforeFireWall == 8 && kSimRulesBeforeLastSide == 5 && kSimRulesMode187Heal == 6 && kSimRulesMode187BeforeFireWall == 7 && kSimRulesMode187BeforeEndFix == 10 && kSimRulesMode187 == 11);
        ASSERT_TRUE(sim_rules_for_mode(0) == 9 && sim_rules_for_mode(1) == 11 && sim_rules_for_mode(2) == 0 && sim_rules_for_mode(255) == 0);
        for (uint16_t rules = 0; rules < 14; ++rules) {
            ASSERT_EQ(plays_mode_rules(0, rules), rules == 5 || rules == 8 || rules == 9);
            ASSERT_EQ(plays_mode_rules(k187, rules), rules == 6 || rules == 7 || rules == 10 || rules == 11);
            ASSERT_FALSE(plays_mode_rules(2, rules));
            ASSERT_FALSE(plays_mode_rules(255, rules));
        }
        ASSERT_EQ(played_rules_text(), std::string("5, 6, 7, 8, 9, 10 and 11"));
        Header head;
        head.map_name = "TINY.LVL";
        head.roster = 0x0F;
        ASSERT_EQ(head.sim_rules, 9);                                                        // (a head that nobody changed is a head of the original's game now)
        for (const uint16_t rules : {uint16_t{5}, uint16_t{8}, uint16_t{9}}) {
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
        for (const uint16_t rules : {uint16_t{6}, uint16_t{7}, uint16_t{10}}) {
            Header h = head;
            h.sim_rules = rules;
            Replay r;
            r.head = h;
            std::string error;
            ASSERT_TRUE(encode(r, error).empty() && !plays_here(h));                         // (the rules of 187 without the mode are no file)
        }
        for (const uint16_t rules : {uint16_t{5}, uint16_t{8}, uint16_t{9}}) {
            Header h = head;
            h.set_mode(k187);
            h.sim_rules = rules;
            Replay r;
            r.head = h;
            std::string error;
            ASSERT_TRUE(encode(r, error).empty() && !plays_here(h));                         // (the original's number with the mode: a build that skips field 17 would play it as the original's game)
        }
        {
            Header h = head;
            h.set_mode(k187);
            h.sim_rules = 12;
            Replay r;
            r.head = h;
            std::string error;
            ASSERT_FALSE(encode(r, error).empty());                                          // (a number from a newer build is a file; it is just not played here)
            ASSERT_FALSE(plays_here(h));
            h.mode = 0;                                                                      // (the same number with the original's mode)
            ASSERT_FALSE(plays_here(h));
        }
        // a match that a Recorder starts: the original's game keeps the number its head names (5 and 8 are both ones it writes), a mode that is not 0 gets the number of that mode whatever the head said
        for (const uint16_t said : {uint16_t{5}, uint16_t{6}, uint16_t{7}, uint16_t{8}, uint16_t{9}, uint16_t{10}, uint16_t{11}}) {
            Header h = head;
            h.set_mode(k187);
            h.sim_rules = said;
            Recorder rec(h);
            ASSERT_EQ(rec.replay().head.sim_rules, 11);
        }
        Header plain = head;
        Recorder fresh(plain);
        ASSERT_TRUE(fresh.replay().head.sim_rules == 9 && fresh.replay().head.mode == 0);
        plain.sim_rules = 5;
        Recorder old(plain);
        ASSERT_EQ(old.replay().head.sim_rules, 5);
        // the refusal of a number nobody plays names the seven that this build does
        Header foreign = head;
        foreign.sim_rules = 12;
        ASSERT_TRUE(rules_refusal(foreign).find("simulation rules 5, 6, 7, 8, 9, 10 and 11") != std::string::npos);
        foreign.sim_rules = 4;
        ASSERT_TRUE(rules_refusal(foreign).find("with simulation rules 4; this build plays simulation rules 5, 6, 7, 8, 9, 10 and 11") != std::string::npos);
    } TEST_END();

    TEST_CASE("RP13.2 A recording under the number 5 that hits the state (one side left, no food) and goes on to the clock plays to every hash it holds and to the final hash that the build before the rule made of it") {
        const Made five = make(kSimRulesBeforeLastSide);
        ASSERT_TRUE(five.error.empty() && !five.file.empty());
        ASSERT_TRUE(five.replay.complete && five.replay.head.sim_rules == 5 && five.replay.head.mode == 0 && plays_here(five.replay.head));
        ASSERT_EQ(five.turns, kBeforeTurns);                                                 // (the match ran to the clock: a minute, and the rest of the last second)
        ASSERT_EQ(five.hash, kBeforeFinalHash);                                              // (what the build before the rule made of the very same orders)
        ASSERT_EQ(five.replay.total_turns, kBeforeTurns);
        ASSERT_EQ(five.replay.final_hash, kBeforeFinalHash);
        ASSERT_TRUE(five.replay.hashes.size() == 12 && five.replay.hashes[0] == kBeforeHash100 && five.replay.hashes[3] == kBeforeHash400 && five.replay.hashes[11] == kBeforeHash1200);
        ASSERT_TRUE(five.result.tie_standing == sim::PLAYER_NEUTRAL && five.result.stats[0].score == 0 && five.result.stats[1].score == 0);
        const Outcome o = play(five.replay, level);
        ASSERT_TRUE(o.ran && o.ok && o.complete && o.match_over);
        ASSERT_EQ(o.turns, kBeforeTurns);
        ASSERT_EQ(o.hash, kBeforeFinalHash);
        ASSERT_EQ(o.hashes_checked, 12u);
        ASSERT_EQ(o.first_bad_turn, 0u);
        ASSERT_TRUE(o.result.tie_standing == sim::PLAYER_NEUTRAL);                               // (a tie at 0 and the clock: nobody is put first, the screen's order decides as it always did)
        // written again, the file is the same bytes
        std::string error;
        ASSERT_EQ(encode(five.replay, error), five.file);
    } TEST_END();

    TEST_CASE("RP13.3 The same match recorded under the number 8 ends where the rule ends it, with the survivor first; each file is another match under the other number") {
        const Made five = make(kSimRulesBeforeLastSide);
        const Made eight = make(kSimRulesBeforeFireWall);
        ASSERT_TRUE(eight.error.empty() && !eight.file.empty() && five.error.empty());
        ASSERT_TRUE(eight.replay.complete && eight.replay.head.sim_rules == 8 && plays_here(eight.replay.head));
        ASSERT_TRUE(eight.over && eight.turns > 100 && eight.turns < 600);                   // (the Worker dies after about 17 s; the rule ends the match within a second of that, long before the minute)
        ASSERT_LT(eight.turns, kBeforeTurns);
        ASSERT_TRUE(eight.result.tie_standing == 0 && eight.result.standing == sim::PLAYER_NEUTRAL && eight.result.winning_players == std::vector<uint8_t>({0}) && eight.result.losing_players == std::vector<uint8_t>({1}));
        ASSERT_TRUE(eight.result.stats[0].score == 0 && eight.result.stats[1].score == 0);   // (a tie at 0: the original's rule would never have let it win)
        ASSERT_TRUE(eight.result.rows(1)[0].first == 0);                                     // (not even the screen of the loser puts itself first)
        const Outcome o = play(eight.replay, level);
        ASSERT_TRUE(o.ran && o.ok && o.complete && o.match_over);
        ASSERT_EQ(o.turns, eight.turns);
        ASSERT_EQ(o.hash, eight.hash);
        ASSERT_EQ(o.first_bad_turn, 0u);
        ASSERT_TRUE(o.result.tie_standing == 0 && o.result.winning_players == std::vector<uint8_t>({0}));
        // the two files hold the same commands up to the end of the short one
        ASSERT_TRUE(!five.replay.commands.empty() && eight.replay.commands.size() <= five.replay.commands.size());
        for (size_t i = 0; i < eight.replay.commands.size(); ++i) {
            ASSERT_EQ(eight.replay.commands[i].turn, five.replay.commands[i].turn);
            ASSERT_TRUE(eight.replay.commands[i].command.ants == five.replay.commands[i].command.ants);
        }
        // the file of 5 under the number 8 is another match (it ends early, so the hashes after that turn differ), and the file of 8 under 5 as well (it goes on to the clock)
        const Outcome five_as_eight = play(with_rules(five.replay, 8), level);
        ASSERT_FALSE(five_as_eight.ok);
        ASSERT_TRUE(five_as_eight.first_bad_turn > 0 || five_as_eight.hash != kBeforeFinalHash);
        ASSERT_EQ(five_as_eight.match_over, true);
        const Outcome eight_as_five = play(with_rules(eight.replay, 5), level);
        ASSERT_FALSE(eight_as_five.ok);
        // and a number nobody plays is not played at all
        ASSERT_FALSE(play(with_rules(eight.replay, 11), level).ran);
        ASSERT_FALSE(play(with_rules(eight.replay, 6), level).ran);
    } TEST_END();

    TEST_CASE("RP13.4 A copy of the engine in the middle of the match ends at the same turn with the same hash, under 8 and under 5 (a client's predicted engine is such a copy)") {
        for (const uint16_t rules : {kSimRulesBeforeFireWall, kSimRulesBeforeLastSide}) {
            sim::SimulationEngine engine;
            engine.set_game_mode(sim::GameMode::HighestScore);
            engine.set_last_side_rule(rules == kSimRulesBeforeFireWall);
            engine.init(level, scenario::kSeed, scenario::kRoster);
            uint32_t turn = 0;
            const auto step = [&](sim::SimulationEngine& e) {
                for (const sim::Command& c : scenario::orders(e, turn)) e.apply_command(c);
                e.tick();
                e.clear_news_events();
                e.clear_audio_events();
            };
            for (; turn < 200; ++turn) step(engine);
            sim::SimulationEngine copy(engine);
            for (; turn < 4000 && (!engine.is_match_over() || !copy.is_match_over()); ++turn) {
                if (!engine.is_match_over()) step(engine);
                if (!copy.is_match_over()) {
                    for (const sim::Command& c : scenario::orders(copy, turn)) copy.apply_command(c);
                    copy.tick();
                    copy.clear_news_events();
                    copy.clear_audio_events();
                }
                ASSERT_EQ(engine.is_match_over(), copy.is_match_over());
                ASSERT_TRUE(engine.state_hash() == copy.state_hash());
            }
            ASSERT_TRUE(engine.is_match_over() && copy.is_match_over());
            ASSERT_EQ(turn < kBeforeTurns, rules == kSimRulesBeforeFireWall);
            ASSERT_TRUE(engine.get_world_state().match_result.tie_standing == copy.get_world_state().match_result.tie_standing);
            ASSERT_EQ(engine.last_side_rule(), copy.last_side_rule());
            ASSERT_EQ(engine.last_side_rule(), rules == kSimRulesBeforeFireWall);
        }
    } TEST_END();

    std::cout << "\n=======================================================\n"
              << " TEST SUMMARY: Total test cases: " << g_test_count << ", Total assertions: " << g_assert_count << ", Failed: " << g_test_failures << "\n"
              << "=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}
