// Tests of the rules number 11 of game mode 187 (ants_replay/replay.hpp, docs/REPLAYS.md "Which files play: sim_rules", docs/GAMEPLAY.md "187"): the end rule counts the teams that began, not the seats of the roster, and
// a team that holds only eggs that cannot hatch is not standing. The numbers 6, 7 and 10 (187) keep the rules before, and every number belongs to one mode. This build plays 5, 8 and 9 for the original's game and
// 6, 7, 10 and 11 for 187.
//   RP15.1  the numbers: what a build plays, what a head may say, what a match is recorded under, what a refusal says
//   RP15.2  the recordings of the old numbers (10, 7 and 6), made in code on the two matches of end_fix_scenario.hpp, play to the hashes that the build before the number made of them
//   RP15.3  the same matches recorded under 11 end where the new rule ends them (a match of one team plays on, the last team standing wins after a drop); each file is another match under the other number
//   RP15.4  a copy of the engine in the middle of the match keeps the rules of its number
#include "ants_replay/player.hpp"
#include "ants_replay/recorder.hpp"
#include "ants_replay/replay.hpp"
#include "ants_sim/sim_engine.hpp"
#include "end_fix_scenario.hpp"

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

namespace scenario = ants::end_fix_scenario;
constexpr uint8_t k187 = static_cast<uint8_t>(sim::GameMode::Kills187);

// What the build before the number 11 made of the two matches of end_fix_scenario.hpp under the numbers 10, 7 and 6: the turns it ran, the engine's state hash at the end, and a fold of every hash the recording holds
// (FNV-1a over the 32-bit hashes, one a second of play). Taken by playing the same header against that build (the merge of the loader's rules numbers 9 and 10, 459b807a), not against this one.
struct Pinned {
    uint32_t turns;
    uint64_t final_hash;
    size_t hashes;
    uint64_t fold;
};
// lone: one team on the map, a seat without a team: the match ends at its first look, seat 0 the winner
constexpr Pinned kLone10 = {1, 0x85a457fc3b2c22bbull, 0, 0x14650fb0739d0383ull};
constexpr Pinned kLone7 = {1, 0x85a457fc3b2c22bbull, 0, 0x14650fb0739d0383ull};
constexpr Pinned kLone6 = {1, 0x8f7ef43ee73b88daull, 0, 0x14650fb0739d0383ull};
// ghost: a seat that holds eggs and nothing else keeps the match going after the drop of seat 1: it plays to the clock
constexpr Pinned kGhost10 = {1205, 0xa6543de1366a43feull, 12, 0x022e3ceddc473ab2ull};
constexpr Pinned kGhost7 = {1205, 0xa6543de1366a43feull, 12, 0x022e3ceddc473ab2ull};
constexpr Pinned kGhost6 = {1205, 0xabacad7494fb7d84ull, 12, 0x44c240e98bd7207full};
// What this build makes of the same matches under the number 11
constexpr Pinned kLone11 = {1205, 0x063e826507a9679eull, 12, 0xe12c34262ded08f5ull};
constexpr Pinned kGhost11 = {101, 0x8ec39b6c865bd9b1ull, 1, 0x12496bcf05124859ull};

struct Made {
    std::vector<uint8_t> file;
    Replay replay;
    uint32_t turns{0};
    uint64_t hash{0};
    bool over{false};
    uint8_t standing{sim::PLAYER_NEUTRAL};
    std::string error;
};

uint64_t fold_of(const std::vector<uint32_t>& hashes) {
    uint64_t fold = 1469598103934665603ull;
    for (const uint32_t h : hashes) fold = (fold ^ h) * 1099511628211ull;
    return fold;
}

Header scenario_head(bool ghost, uint16_t rules) {
    Header head;
    head.game_version = "v0.0.0";
    head.build_id = "test";
    head.venue = "local game";
    head.map_name = "SYNTH.LVL";
    head.seed = scenario::kSeed;
    head.roster = scenario::roster(ghost);
    head.set_mode(k187);
    head.sim_rules = rules;
    return head;
}

// The match of the scenario, set up as the player of a recording of the number `rules` sets it up (begin_match), played and recorded as a game on one machine does it. A file is stamped with its number after the play
// (a Recorder writes the number of now for a mode that is not 0), which is what a build of that number wrote.
Made make(bool ghost, uint16_t rules, const assets::LevelData& level) {
    Made m;
    const Header head = scenario_head(ghost, rules);
    Replay shell;
    shell.head = head;
    sim::SimulationEngine engine;
    begin_match(engine, shell, level, false);
    Recorder rec(head);
    uint32_t turn = 0;
    for (; turn < 4000 && !engine.is_match_over(); ++turn) {
        for (const sim::Command& c : scenario::orders(ghost, turn)) {
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
    m.standing = engine.get_world_state().match_result.standing;
    m.file = rec.finish(engine, m.error);
    if (!m.file.empty()) {
        std::string why;
        if (!decode(m.file.data(), m.file.size(), m.replay, why)) m.error = why;
        if (m.replay.head.sim_rules != rules) {
            m.replay.head.sim_rules = rules;                                                 // (a match of the number 10, 7 or 6: the Recorder wrote 11)
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
    std::cout << "=== Rules number 11 of game mode 187 (6, 7 and 10 keep the end rule of the seats and of any egg) ===\n";
    const assets::LevelData lone = scenario::level(false);
    const assets::LevelData ghost = scenario::level(true);

    TEST_CASE("RP15.1 This build plays 5, 8 and 9 for the original's game and 6, 7, 10 and 11 for 187, nothing else; a 187 match is recorded under 11; every number names its one mode; a refusal names all seven") {
        ASSERT_TRUE(kSimRulesMode187 == 11 && kSimRulesMode187BeforeEndFix == 10 && kSimRulesMode187BeforeFireWall == 7 && kSimRulesMode187Heal == 6);
        ASSERT_TRUE(kSimRules == 9 && kSimRulesBeforeFireWall == 8 && kSimRulesBeforeLastSide == 5 && kSimRulesNewest == 11);
        ASSERT_TRUE(sim_rules_for_mode(0) == 9 && sim_rules_for_mode(1) == 11 && sim_rules_for_mode(2) == 0 && sim_rules_for_mode(255) == 0);
        for (uint16_t rules = 0; rules < 16; ++rules) {
            ASSERT_EQ(plays_mode_rules(0, rules), rules == 5 || rules == 8 || rules == 9);
            ASSERT_EQ(plays_mode_rules(k187, rules), rules == 6 || rules == 7 || rules == 10 || rules == 11);
            ASSERT_FALSE(plays_mode_rules(2, rules));
            ASSERT_FALSE(plays_mode_rules(255, rules));
            ASSERT_EQ(fire_wall_as_original_rules(rules), rules >= 9);                       // (10 and 11 have the loader of the original, as 9 does)
        }
        ASSERT_EQ(played_rules_text(), std::string("5, 6, 7, 8, 9, 10 and 11"));
        Header head;
        head.map_name = "TINY.LVL";
        head.roster = 0x0F;
        for (uint16_t rules = 0; rules < 14; ++rules) {
            for (const uint8_t mode : {uint8_t{0}, k187}) {
                Header h = head;
                h.set_mode(mode);
                h.sim_rules = rules;
                Replay r;
                r.head = h;
                std::string error;
                const std::vector<uint8_t> bytes = encode(r, error);
                const bool mode_has_it = mode == 0 ? (rules == 5 || rules == 8 || rules == 9) : (rules == 6 || rules == 7 || rules == 10 || rules == 11);
                const bool other_mode_has_it = mode == 0 ? (rules == 6 || rules == 7 || rules == 10 || rules == 11) : (rules == 5 || rules == 8 || rules == 9);
                ASSERT_EQ(plays_here(h), mode_has_it);
                ASSERT_EQ(!bytes.empty(), !other_mode_has_it && (mode == 0 || rules >= 6));      // (a number of another mode is no file; 4 and 12 are files, only not played)
                if (!bytes.empty()) {
                    Replay back;
                    ASSERT_TRUE(decode(bytes.data(), bytes.size(), back, error) && back.head.sim_rules == rules && back.head.mode == mode && plays_here(back.head) == mode_has_it);
                }
            }
        }
        for (const uint16_t said : {uint16_t{5}, uint16_t{6}, uint16_t{7}, uint16_t{8}, uint16_t{9}, uint16_t{10}, uint16_t{11}}) {
            Header h = head;
            h.set_mode(k187);
            h.sim_rules = said;
            Recorder rec(h);
            ASSERT_EQ(rec.replay().head.sim_rules, 11);                                      // (a mode that is not 0 gets the number of that mode whatever the head said)
        }
        Recorder fresh(head);
        ASSERT_TRUE(fresh.replay().head.sim_rules == 9 && fresh.replay().head.mode == 0);
        Header foreign = head;
        foreign.sim_rules = 12;
        ASSERT_TRUE(rules_refusal(foreign).find("simulation rules 12; this build plays simulation rules 5, 6, 7, 8, 9, 10 and 11") != std::string::npos);
    } TEST_END();

    TEST_CASE("RP15.2 A recording of the number 10, 7 or 6, made in code on the two matches of the scenario, plays to every hash it holds and to the final hash that the build before the number made of it") {
        ASSERT_TRUE(plays_to(make(false, 10, lone), kLone10, lone, 10));
        ASSERT_TRUE(plays_to(make(false, 7, lone), kLone7, lone, 7));
        ASSERT_TRUE(plays_to(make(false, 6, lone), kLone6, lone, 6));
        ASSERT_TRUE(plays_to(make(true, 10, ghost), kGhost10, ghost, 10));
        ASSERT_TRUE(plays_to(make(true, 7, ghost), kGhost7, ghost, 7));
        ASSERT_TRUE(plays_to(make(true, 6, ghost), kGhost6, ghost, 6));
        // the old matches really are the ones the fixes change: seat 1, a seat without a team, makes seat 0 the winner at once; the seat that holds eggs only keeps the clock running after the drop
        const Made old_lone = make(false, 10, lone);
        ASSERT_TRUE(old_lone.over && old_lone.turns == 1 && old_lone.standing == 0);
        const Made old_ghost = make(true, 10, ghost);
        ASSERT_TRUE(old_ghost.over && old_ghost.turns > 1000 && old_ghost.standing == sim::PLAYER_NEUTRAL);
        // written again, the files are the same bytes
        std::string error;
        ASSERT_EQ(encode(old_lone.replay, error), old_lone.file);
        ASSERT_EQ(encode(old_ghost.replay, error), old_ghost.file);
    } TEST_END();

    TEST_CASE("RP15.3 The same matches recorded under the number 11 end where the new rule ends them: one team on the map plays on to the clock, and after the drop the team that is left wins at once; each file is another match under the other number") {
        const Made one = make(false, 11, lone);
        ASSERT_TRUE(one.error.empty() && one.over && one.turns > 1000 && one.standing == sim::PLAYER_NEUTRAL);        // (a match of one team has no one to beat: the clock ends it)
        ASSERT_TRUE(plays_to(one, kLone11, lone, 11));
        const Made two = make(true, 11, ghost);
        ASSERT_TRUE(two.error.empty() && two.over && two.turns == 101 && two.standing == 0);        // (turn 100 the drop; the rule looks every 200 ms: seat 0 stands, the seat of eggs is no team)
        ASSERT_TRUE(plays_to(two, kGhost11, ghost, 11));
        ASSERT_TRUE(one.hash != kLone10.final_hash && two.hash != kGhost10.final_hash);
        // the commands are the same: only the end rule differs
        ASSERT_TRUE(two.replay.commands.size() == 1 && make(true, 10, ghost).replay.commands.size() == 1);
        // a file played under another number of its mode is another match
        const Outcome ten_as_eleven = play(with_rules(make(true, 10, ghost).replay, 11), ghost);
        ASSERT_TRUE(!ten_as_eleven.ok);
        const Outcome eleven_as_ten = play(with_rules(two.replay, 10), ghost);
        ASSERT_TRUE(!eleven_as_ten.ok);
        ASSERT_TRUE(eleven_as_ten.first_bad_turn > 0u);
        const Outcome lone_as_ten = play(with_rules(one.replay, 10), lone);
        ASSERT_TRUE(!lone_as_ten.ok);
        // a number nobody plays is not played
        ASSERT_FALSE(play(with_rules(two.replay, 12), ghost).ran);
        ASSERT_FALSE(play(with_rules(two.replay, 9), ghost).ran);
    } TEST_END();

    TEST_CASE("RP15.4 A copy of the engine in the middle of the match ends at the same turn with the same hash, under 11 and under 10 (a client's predicted engine is such a copy)") {
        for (const uint16_t rules : {kSimRulesMode187, kSimRulesMode187BeforeEndFix}) {
            Replay shell;
            shell.head = scenario_head(true, rules);
            sim::SimulationEngine engine;
            begin_match(engine, shell, ghost, false);
            ASSERT_EQ(engine.end_fix_187(), rules == kSimRulesMode187);
            uint32_t turn = 0;
            const auto step = [&](sim::SimulationEngine& e) {
                for (const sim::Command& c : scenario::orders(true, turn)) e.apply_command(c);
                e.tick();
                e.clear_news_events();
                e.clear_audio_events();
            };
            for (; turn < 50; ++turn) step(engine);
            sim::SimulationEngine copy(engine);
            ASSERT_EQ(copy.end_fix_187(), rules == kSimRulesMode187);
            for (; turn < 4000 && (!engine.is_match_over() || !copy.is_match_over()); ++turn) {
                if (!engine.is_match_over()) step(engine);
                if (!copy.is_match_over()) {
                    for (const sim::Command& c : scenario::orders(true, turn)) copy.apply_command(c);
                    copy.tick();
                    copy.clear_news_events();
                    copy.clear_audio_events();
                }
                if (engine.is_match_over() && copy.is_match_over()) break;
            }
            ASSERT_TRUE(engine.is_match_over() && copy.is_match_over());
            ASSERT_EQ(copy.state_hash().total, engine.state_hash().total);
            ASSERT_EQ(copy.get_world_state().match_result.standing, engine.get_world_state().match_result.standing);
            ASSERT_TRUE((turn < 200) == (rules == kSimRulesMode187));
        }
    } TEST_END();

    std::cout << "\n=======================================================\n"
              << " TEST SUMMARY: Total test cases: " << g_test_count << ", Total assertions: " << g_assert_count << ", Failed: " << g_test_failures << "\n"
              << "=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}
