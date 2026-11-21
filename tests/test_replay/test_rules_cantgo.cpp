// Tests of the rules number 14 of the original's game and 15 of 187 (ants_replay/replay.hpp, docs/REPLAYS.md "Which files play: sim_rules", docs/GAME_REVERSE_ENGINEERING.md 5.40, "The gap before the path manager's answer"): from
// 14 (187: 15) the path manager's refusal ("no path") is given at the start of the next tick, after the turn's commands, so an ant that an event has just sent takes an order; the numbers before (5, 8, 9 and 13 of the original's game,
// 6, 7, 10, 11 and 12 of 187) give it at once, as they always did. This build plays 5, 8, 9, 13 and 14 for the original's game and 6, 7, 10, 11, 12 and 15 for 187, and every number belongs to one mode.
//   RP18.1  the numbers: what a build plays, which of them have the rule, what a head may say, what a match is recorded under, what a refusal says, how the player of a recording sets the engine up
//   RP18.2  a recording under 13 on the match of cantgo_scenario.hpp (a thief that is shut out of its hill, ordered to a free tile every turn) plays to every hash and to the final hash that the build before the rule made of it,
//           with the thief still on the raid tile at the end: no order met it between a clip and the manager's refusal
//   RP18.3  the same match recorded under 14 is another match in which the thief walks away at the first turn that finds it without a path and stays on the free tile; each file is another match under the other number
//   RP18.4  game mode 187 under 12 and 15: the same two plays by the rules of its numbers
//   RP18.5  the engine copy and the rule: a copy of the engine in the middle of the match keeps the rule of its number and plays on to the same hash
#include "ants_replay/player.hpp"
#include "ants_replay/recorder.hpp"
#include "ants_replay/replay.hpp"
#include "ants_sim/sim_engine.hpp"
#include "cantgo_scenario.hpp"

#include <cstdint>
#include <functional>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>
#include "../common/test_assert.hpp"

using namespace ants;
using namespace ants::replay;
namespace scenario = ants::cantgo_scenario;

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

// What the build before the rule made of the match of cantgo_scenario.hpp under the number 13 and, for game mode 187, under 12 (it ends on the clock, one minute): the turns it ran, the engine's state hash at the end, and
// the low 32 bits of the hash after turns 100, 400 and 1100. Taken by playing the same header against that build (the commit that this one grew from), not by this one.
constexpr uint32_t kBeforeTurns = 1205;
constexpr uint64_t kBeforeFinalHash = 0x6898d060933acf64ull;
constexpr uint32_t kBeforeHash100 = 0x307ec777;
constexpr uint32_t kBeforeHash400 = 0x9f4bd033;
constexpr uint32_t kBeforeHash1100 = 0x2290b500;
constexpr uint64_t kBefore187FinalHash = 0xfba19d022512cb18ull;
constexpr uint32_t kBefore187Hash400 = 0xcbfed595;

struct Made {
    std::vector<uint8_t> file;
    Replay replay;
    uint32_t turns{0};
    uint64_t hash{0};
    sim::TileCoord thief{-1, -1};                              // where seat 0's Thief stands at the end
    std::string error;
};

Header scenario_head(uint16_t rules, uint8_t mode = 0) {
    Header head;
    head.game_version = "v0.0.0";
    head.build_id = "test";
    head.venue = "local game";
    head.map_name = "SYNTH.LVL";
    head.seed = scenario::kSeed;
    head.roster = scenario::kRoster;
    head.set_mode(mode);
    head.sim_rules = rules;
    return head;
}

// The match of the scenario, played and recorded as a game on one machine does it, by the rules of the number `rules` of game mode `mode`
Made make(uint16_t rules, uint8_t mode = 0) {
    Made m;
    const assets::LevelData level = scenario::level();
    sim::SimulationEngine engine;
    engine.set_game_mode(static_cast<sim::GameMode>(mode));
    engine.set_path_answers_after_commands(path_answers_after_commands_rules(rules));
    engine.set_no_drops_187(!(mode == k187 && rules < kSimRulesMode187BeforeCantGo));
    engine.init(level, scenario::kSeed, scenario::kRoster);
    Recorder rec(scenario_head(rules, mode));
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
    m.thief = scenario::thief_tile(engine);
    m.file = rec.finish(engine, m.error);
    if (!m.file.empty()) {
        std::string why;
        if (!decode(m.file.data(), m.file.size(), m.replay, why)) m.error = why;
        if (m.error.empty() && m.replay.head.sim_rules != rules) {     // (a recorder writes the number of its mode now: a file of an older number of 187 is this file with the number changed, as the match was played by that number's rules)
            m.replay.head.sim_rules = rules;
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

// The tile of the thief at the end of the match that `replay` plays (the player's own hook: the engine at the last turn)
sim::TileCoord thief_after_playing(const Replay& replay, const assets::LevelData& level, Outcome& outcome) {
    sim::TileCoord tile{-1, -1};
    Hooks hooks;
    hooks.after_tick = [&](uint32_t, sim::SimulationEngine& engine) { tile = scenario::thief_tile(engine); };
    outcome = play(replay, level, hooks);
    return tile;
}

}  // namespace

int main() {
    std::cout << "=== Rules number 14 of the original's game and 15 of 187: the path manager's refusal after the commands ===\n";
    const assets::LevelData level = scenario::level();

    TEST_CASE("RP18.1 The original's game is recorded under 14 and played under 5, 8, 9, 13 and 14; 187 under 15 and 6, 7, 10, 11, 12 and 15; only 14 and 15 give the path manager's refusal after the commands; a refusal names all eleven") {
        ASSERT_TRUE(kSimRules == 14 && kSimRulesBeforeCantGo == 13 && kSimRulesCantGoFirst == 14 && kSimRulesBeforeEmptyRecord == 9 && kSimRulesBeforeFireWall == 8 && kSimRulesBeforeLastSide == 5);
        ASSERT_TRUE(kSimRulesMode187 == 15 && kSimRulesMode187BeforeCantGo == 12 && kSimRulesMode187BeforeNoDrops == 11 && kSimRulesMode187BeforeEndFix == 10 && kSimRulesMode187BeforeFireWall == 7 && kSimRulesMode187Heal == 6 && kSimRulesNewest == 15);
        ASSERT_TRUE(sim_rules_for_mode(0) == 14 && sim_rules_for_mode(1) == 15);
        for (uint16_t rules = 0; rules < 18; ++rules) {
            ASSERT_EQ(plays_mode_rules(0, rules), rules == 5 || rules == 8 || rules == 9 || rules == 13 || rules == 14);
            ASSERT_EQ(plays_mode_rules(k187, rules), rules == 6 || rules == 7 || rules == 10 || rules == 11 || rules == 12 || rules == 15);
            ASSERT_EQ(path_answers_after_commands_rules(rules), rules >= 14);
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
        for (const uint16_t rules : {uint16_t{6}, uint16_t{7}, uint16_t{10}, uint16_t{11}, uint16_t{12}, uint16_t{15}}) {
            Header h = head;
            h.set_mode(k187);
            h.sim_rules = rules;
            Replay r;
            r.head = h;
            std::string error;
            const std::vector<uint8_t> bytes = encode(r, error);
            ASSERT_FALSE(bytes.empty());
            ASSERT_TRUE(plays_here(h));
            Replay back;
            ASSERT_TRUE(decode(bytes.data(), bytes.size(), back, error) && back.head.sim_rules == rules && back.head.mode == k187);
        }
        {
            Header h = head;                                                                 // (187 under the original's numbers, and the original's under 187's: no file)
            Replay r;
            std::string error;
            for (const uint16_t rules : {uint16_t{13}, uint16_t{14}}) {
                h.set_mode(k187);
                h.sim_rules = rules;
                r.head = h;
                ASSERT_TRUE(encode(r, error).empty() && !plays_here(h));
            }
            for (const uint16_t rules : {uint16_t{12}, uint16_t{15}}) {
                h.set_mode(0);
                h.sim_rules = rules;
                r.head = h;
                ASSERT_TRUE(encode(r, error).empty() && !plays_here(h));
            }
        }
        Header plain = head;
        Recorder fresh(plain);
        ASSERT_TRUE(fresh.replay().head.sim_rules == 14 && fresh.replay().head.mode == 0);
        plain.sim_rules = 13;
        Recorder old(plain);
        ASSERT_EQ(old.replay().head.sim_rules, 13);                                          // (a head that says 13 is recorded as 13: the engine that plays it is set up for it)
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
            ASSERT_EQ(engine.path_answers_after_commands(), rules == 14);
        }
        for (const uint16_t rules : {uint16_t{6}, uint16_t{7}, uint16_t{10}, uint16_t{11}, uint16_t{12}, uint16_t{15}}) {
            sim::SimulationEngine engine;
            Replay r;
            r.head = scenario_head(rules, k187);
            begin_match(engine, r, level, false);
            ASSERT_EQ(engine.path_answers_after_commands(), rules == 15);
            ASSERT_EQ(engine.no_drops_187(), rules >= 12);                                   // (the no-drop rule of 12 is kept by 15: the number of 12 moved to kSimRulesMode187BeforeCantGo)
            ASSERT_EQ(engine.end_fix_187(), rules >= 11);
        }
    } TEST_END();

    TEST_CASE("RP18.2 A recording under 13 on the can't-go match plays to every hash it holds and to the final hash that the build before the rule made of it; the thief is still on the raid tile") {
        const Made old = make(kSimRulesBeforeCantGo);
        ASSERT_TRUE(old.error.empty() && !old.file.empty());
        ASSERT_TRUE(old.replay.complete && old.replay.head.sim_rules == 13 && old.replay.head.mode == 0 && plays_here(old.replay.head));
        ASSERT_EQ(old.turns, kBeforeTurns);
        ASSERT_EQ(old.hash, kBeforeFinalHash);                                               // (what the build before the rule made of the very same orders)
        ASSERT_EQ(old.replay.final_hash, kBeforeFinalHash);
        ASSERT_TRUE(old.replay.hashes.size() == 12 && old.replay.hashes[0] == kBeforeHash100 && old.replay.hashes[3] == kBeforeHash400 && old.replay.hashes[10] == kBeforeHash1100);
        ASSERT_EQ(old.thief.x, scenario::kRaidTile.x);                                       // (every order of the last 1,000 turns went by while the thief was in the clip or just out of it with the answer given)
        ASSERT_EQ(old.thief.y, scenario::kRaidTile.y);
        Outcome o;
        const sim::TileCoord at = thief_after_playing(old.replay, level, o);
        ASSERT_TRUE(o.ran && o.ok && o.complete && o.match_over);
        ASSERT_EQ(o.turns, kBeforeTurns);
        ASSERT_EQ(o.hash, kBeforeFinalHash);
        ASSERT_EQ(o.first_bad_turn, 0u);
        ASSERT_TRUE(at.x == scenario::kRaidTile.x && at.y == scenario::kRaidTile.y);
        std::string error;
        ASSERT_EQ(encode(old.replay, error), old.file);                                      // (written again, the file is the same bytes)
        // the files of 9, 8 and 5 play the same match to the same hash: they differ from 13 by the empty record, the loader and the end rule, none of which this match meets
        for (const uint16_t older : {kSimRulesBeforeEmptyRecord, kSimRulesBeforeFireWall, kSimRulesBeforeLastSide}) {
            Outcome other;
            thief_after_playing(with_rules(old.replay, older), level, other);
            ASSERT_TRUE(other.ran && other.ok);
            ASSERT_EQ(other.hash, kBeforeFinalHash);
        }
    } TEST_END();

    TEST_CASE("RP18.3 The same match recorded under 14 is another match in which the thief walks away and stays on the free tile; each file is another match under the other number") {
        const Made old = make(kSimRulesBeforeCantGo);
        const Made now = make(kSimRules);
        ASSERT_TRUE(now.error.empty() && !now.file.empty() && old.error.empty());
        ASSERT_TRUE(now.replay.complete && now.replay.head.sim_rules == 14 && plays_here(now.replay.head));
        ASSERT_TRUE(now.hash != old.hash);
        ASSERT_EQ(now.thief.x, scenario::kFree.x);                                           // (the first turn that found the thief without a path took the order)
        ASSERT_EQ(now.thief.y, scenario::kFree.y);
        Outcome o;
        const sim::TileCoord at = thief_after_playing(now.replay, level, o);
        ASSERT_TRUE(o.ran && o.ok && o.complete);
        ASSERT_EQ(o.turns, now.turns);
        ASSERT_EQ(o.hash, now.hash);
        ASSERT_EQ(o.first_bad_turn, 0u);
        ASSERT_TRUE(at.x == scenario::kFree.x && at.y == scenario::kFree.y);
        // the two files hold the same commands (the orders come at fixed turns)
        ASSERT_EQ(now.replay.commands.size(), old.replay.commands.size());
        for (size_t i = 0; i < old.replay.commands.size(); ++i) {
            ASSERT_EQ(now.replay.commands[i].turn, old.replay.commands[i].turn);
            ASSERT_TRUE(now.replay.commands[i].command.ants == old.replay.commands[i].command.ants);
        }
        // the file of 13 under the number 14 is another match, and the file of 14 under 13 as well
        const Outcome old_as_now = play(with_rules(old.replay, 14), level);
        ASSERT_FALSE(old_as_now.ok);
        ASSERT_TRUE(old_as_now.first_bad_turn > 0);
        const Outcome now_as_old = play(with_rules(now.replay, 13), level);
        ASSERT_FALSE(now_as_old.ok);
        // and a number nobody plays is not played at all
        ASSERT_FALSE(play(with_rules(now.replay, 16), level).ran);
        ASSERT_FALSE(play(with_rules(now.replay, 12), level).ran);
    } TEST_END();

    TEST_CASE("RP18.4 Game mode 187 under 12 plays as the build before made of it, under 15 it has the rule and, with no refused walk of an event in the match, plays alike; the numbers of the other mode are no files") {
        const Made old = make(kSimRulesMode187BeforeCantGo, k187);
        const Made now = make(kSimRulesMode187, k187);
        ASSERT_TRUE(old.error.empty() && now.error.empty() && !old.file.empty() && !now.file.empty());
        ASSERT_TRUE(old.replay.head.sim_rules == 12 && old.replay.head.mode == k187 && plays_here(old.replay.head));
        ASSERT_TRUE(now.replay.head.sim_rules == 15 && now.replay.head.mode == k187 && plays_here(now.replay.head));
        ASSERT_EQ(old.hash, kBefore187FinalHash);
        ASSERT_TRUE(old.replay.hashes.size() >= 4 && old.replay.hashes[3] == kBefore187Hash400);
        ASSERT_EQ(now.hash, old.hash);                                                       // (nothing in this 187 match meets the gap: no event asks for a walk that is refused, so it plays as before; the files differ by their number)
        ASSERT_TRUE(now.replay.hashes == old.replay.hashes && now.file != old.file);
        for (const Made* m : {&old, &now}) {
            Outcome o;
            thief_after_playing(m->replay, level, o);
            ASSERT_TRUE(o.ran && o.ok && o.complete);
            ASSERT_EQ(o.hash, m->hash);
            ASSERT_EQ(o.first_bad_turn, 0u);
        }
        ASSERT_FALSE(play(with_rules(old.replay, 13), level).ran);                           // (13 and 14 are the original's game's numbers)
        ASSERT_FALSE(play(with_rules(now.replay, 14), level).ran);
        ASSERT_TRUE(play(with_rules(now.replay, 12), level).ok);                             // (12 plays it too, to the same hashes: this match has no refused walk of an event, which is all that the two numbers differ by)
    } TEST_END();

    TEST_CASE("RP18.5 A copy of the engine in the middle of the match keeps the rule of its number and plays on to the same hash, under 14 and under 13 (a client's predicted engine is such a copy)") {
        for (const uint16_t rules : {kSimRules, kSimRulesBeforeCantGo}) {
            sim::SimulationEngine engine;
            engine.set_game_mode(sim::GameMode::HighestScore);
            engine.set_path_answers_after_commands(path_answers_after_commands_rules(rules));
            engine.init(level, scenario::kSeed, scenario::kRoster);
            uint32_t turn = 0;
            const auto step = [&](sim::SimulationEngine& e) {
                for (const sim::Command& c : scenario::orders(e, turn)) e.apply_command(c);
                e.tick();
                e.clear_news_events();
                e.clear_audio_events();
            };
            for (; turn < 500; ++turn) step(engine);                                         // (in the loop: the copy is made with the thief in the clip or in the gap)
            sim::SimulationEngine copy(engine);
            for (; turn < 1100; ++turn) {
                step(engine);
                step(copy);
                ASSERT_TRUE(engine.state_hash() == copy.state_hash());
            }
            ASSERT_EQ(copy.path_answers_after_commands(), rules == kSimRules);
            ASSERT_EQ(engine.path_answers_after_commands(), copy.path_answers_after_commands());
        }
    } TEST_END();

    std::cout << "\n=======================================================\n"
              << " TEST SUMMARY: Total test cases: " << g_test_count << ", Total assertions: " << g_assert_count << ", Failed: " << g_test_failures << "\n"
              << "=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}
