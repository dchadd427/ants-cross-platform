// Tests of the rules numbers that go with the loader's fire wall (ants_replay/replay.hpp, docs/REPLAYS.md "Which files play: sim_rules", docs/reverse_engineering/map_loader_fidelity.md section 8): a fire wall of a
// map file loads as the original loads it, with no obstacle overlay from its dictionary entry, from the rules number 9 (the original's game) and 10 (game mode 187) on. The numbers 5, 8 (the original's game) and
// 6, 7 (187) keep the old loader, and every number belongs to one mode. This build plays all six.
//   RP14.1  the numbers: what a build plays, what a head may say, what a match is recorded under, which numbers have the loader, what a refusal says, a number nobody plays
//   RP14.2  the recordings of the old numbers (8 and 7), made in code on a map with a named fire wall, play to the hashes that the build before the number made of them
//   RP14.3  the same match recorded under the new numbers (9 and 10) is another match from the first hash; the files play to their pinned hashes
//   RP14.4  a recording played under another number of its mode is another match, and a number nobody plays is not played; the engine's copy keeps the loader
#include "ants_replay/player.hpp"
#include "ants_replay/recorder.hpp"
#include "ants_replay/replay.hpp"
#include "ants_sim/sim_engine.hpp"
#include "fire_wall_scenario.hpp"

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

namespace scenario = ants::fire_wall_scenario;
constexpr uint8_t k187 = static_cast<uint8_t>(sim::GameMode::Kills187);

// What the build before the rules numbers 9 and 10 made of the match of fire_wall_scenario.hpp under the numbers 8 (the original's game) and 7 (187): the turns it ran, the engine's state hash at the end, and the low 32
// bits of the hash after turns 100, 400 and 1200. Taken by playing the same header against that build (the commit this one grew from, tools/hash_ledger's base), not against this one.
struct Pinned {
    uint32_t turns;
    uint64_t final_hash;
    uint32_t hash100;
    uint32_t hash400;
    uint32_t hash1200;
};
constexpr Pinned kBefore8 = {1205, 0xdadc93dc74a9e5acull, 0x5e2750e0, 0xb46daa00, 0x923e306f};
constexpr Pinned kBefore7 = {1205, 0x0e0dda130e3870daull, 0x48b2b1af, 0xc491acd5, 0xea2f0425};
// What this build makes of the same match under the numbers 9 and 10 (the loader of the original): another match from tick 0
constexpr Pinned kNow9 = {1205, 0x6d2adac92357f732ull, 0x331c22ff, 0xda5ce6cf, 0xf7b9b881};
constexpr Pinned kNow10 = {1205, 0xe15502716c4e668cull, 0xb7c8a1d0, 0xd2f6a272, 0xbb816833};

struct Made {
    std::vector<uint8_t> file;
    Replay replay;
    uint32_t turns{0};
    uint64_t hash{0};
    bool over{false};
    bool fire_out{false};                  // the Fire Ant put the fire wall of the file out
    int powerups{0};                       // power-ups on the map at the end: the one that the dead Bomber dropped
    bool bomber_dead{false};
    std::string error;
};

Header scenario_head(uint8_t mode, uint16_t rules) {
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

// The match of the scenario, set up as the player of a recording of the number `rules` sets it up (begin_match), played and recorded as a game on one machine does it. A 187 file is stamped with its number after
// the play (a Recorder writes the number of now for a mode that is not 0), which is what a build of the number 7 wrote.
Made make(uint8_t mode, uint16_t rules, const assets::LevelData& level) {
    Made m;
    const Replay shell = [&] {
        Replay r;
        r.head = scenario_head(mode, rules);
        return r;
    }();
    sim::SimulationEngine engine;
    begin_match(engine, shell, level, false);
    Recorder rec(shell.head);
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
    m.fire_out = !engine.has_fire_at(scenario::kWall);
    for (int y = 0; y < 40; ++y)
        for (int x = 0; x < 40; ++x) m.powerups += engine.grid().has_powerup_at(sim::TileCoord{x, y}) ? 1 : 0;
    m.bomber_dead = true;
    for (const sim::AntSnapshot& a : engine.get_world_state().ants) {
        if (a.player_id == 1 && a.type == sim::AntType::Bomber && a.hp != 0) m.bomber_dead = false;
    }
    m.file = rec.finish(engine, m.error);
    if (!m.file.empty()) {
        std::string why;
        if (!decode(m.file.data(), m.file.size(), m.replay, why)) m.error = why;
        if (m.replay.head.sim_rules != rules) {
            m.replay.head.sim_rules = rules;                                                 // (a 187 match of the number 7: the Recorder wrote 11)
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

void expect_plays_to(const Made& m, const Pinned& pinned, const assets::LevelData& level, uint16_t rules, uint8_t mode, bool& ok) {
    if (m.replay.hashes.size() >= 12 && (m.turns != pinned.turns || m.hash != pinned.final_hash || m.replay.hashes[0] != pinned.hash100 || m.replay.hashes[3] != pinned.hash400 || m.replay.hashes[11] != pinned.hash1200)) {
        std::cout << "\n    rules " << rules << " mode " << int(mode) << ": the match ran " << m.turns << " turns and ends in " << std::hex << m.hash << ", hashes after turns 100, 400, 1200: " << m.replay.hashes[0] << " " << m.replay.hashes[3] << " "
                  << m.replay.hashes[11] << std::dec << "\n";
    }
    ok = m.error.empty() && !m.file.empty() && m.replay.complete && m.replay.head.sim_rules == rules && m.replay.head.mode == mode && plays_here(m.replay.head);
    ok = ok && m.turns == pinned.turns && m.hash == pinned.final_hash && m.replay.total_turns == pinned.turns && m.replay.final_hash == pinned.final_hash;
    ok = ok && m.replay.hashes.size() >= 12 && m.replay.hashes[0] == pinned.hash100 && m.replay.hashes[3] == pinned.hash400 && m.replay.hashes[11] == pinned.hash1200;
    const Outcome o = play(m.replay, level);
    ok = ok && o.ran && o.ok && o.complete && o.hash == pinned.final_hash && o.turns == pinned.turns && o.first_bad_turn == 0u;
}

}  // namespace

int main() {
    std::cout << "=== Rules numbers of the loader's fire wall: 9 and 10 (5 to 8 keep the old loader) ===\n";
    const assets::LevelData level = scenario::level();

    TEST_CASE("RP14.1 This build plays 5, 8 and 9 for the original's game and 6, 7, 10 and 11 for 187, nothing else; a match is recorded under 9 or 11; every number names its one mode; the loader of the original is the numbers from 9 on; a refusal names all seven") {
        ASSERT_TRUE(kSimRules == 9 && kSimRulesBeforeFireWall == 8 && kSimRulesBeforeLastSide == 5);
        ASSERT_TRUE(kSimRulesMode187 == 11 && kSimRulesMode187BeforeEndFix == 10 && kSimRulesMode187BeforeFireWall == 7 && kSimRulesMode187Heal == 6);
        ASSERT_TRUE(kSimRulesFireWallFirst == 9 && kSimRulesNewest == 11);
        ASSERT_TRUE(sim_rules_for_mode(0) == 9 && sim_rules_for_mode(1) == 11 && sim_rules_for_mode(2) == 0 && sim_rules_for_mode(255) == 0);
        for (uint16_t rules = 0; rules < 16; ++rules) {
            ASSERT_EQ(plays_mode_rules(0, rules), rules == 5 || rules == 8 || rules == 9);
            ASSERT_EQ(plays_mode_rules(k187, rules), rules == 6 || rules == 7 || rules == 10 || rules == 11);
            ASSERT_FALSE(plays_mode_rules(2, rules));
            ASSERT_FALSE(plays_mode_rules(255, rules));
            ASSERT_EQ(fire_wall_as_original_rules(rules), rules >= 9);                       // (5, 6, 7 and 8 keep the old loader; 9, 10 and every number after them have the original's)
        }
        ASSERT_EQ(played_rules_text(), std::string("5, 6, 7, 8, 9, 10 and 11"));
        Header head;
        head.map_name = "TINY.LVL";
        head.roster = 0x0F;
        ASSERT_EQ(head.sim_rules, 9);                                                        // (a head that nobody changed is a head of the original's game now)
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
                ASSERT_EQ(!bytes.empty(), !other_mode_has_it && (mode == 0 || rules >= 6));      // (a number of another mode is no file; a mode with a rules number that predates it is none either; 4 and 12 are files, only not played)
                if (!bytes.empty()) {
                    Replay back;
                    ASSERT_TRUE(decode(bytes.data(), bytes.size(), back, error) && back.head.sim_rules == rules && back.head.mode == mode && plays_here(back.head) == mode_has_it);
                }
            }
        }
        // a match that a Recorder starts: the original's game keeps the number its head names (5, 8 and 9 are the ones it writes), a mode that is not 0 gets the number of that mode whatever the head said
        for (const uint16_t said : {uint16_t{5}, uint16_t{6}, uint16_t{7}, uint16_t{8}, uint16_t{9}, uint16_t{10}, uint16_t{11}}) {
            Header h = head;
            h.set_mode(k187);
            h.sim_rules = said;
            Recorder rec(h);
            ASSERT_EQ(rec.replay().head.sim_rules, 11);
        }
        Recorder fresh(head);
        ASSERT_TRUE(fresh.replay().head.sim_rules == 9 && fresh.replay().head.mode == 0);
        for (const uint16_t old : {uint16_t{5}, uint16_t{8}}) {
            Header h = head;
            h.sim_rules = old;
            Recorder rec(h);
            ASSERT_EQ(rec.replay().head.sim_rules, old);
        }
        // the refusal of a number nobody plays names the seven that this build does
        Header foreign = head;
        foreign.sim_rules = 12;
        ASSERT_TRUE(rules_refusal(foreign).find("simulation rules 12; this build plays simulation rules 5, 6, 7, 8, 9, 10 and 11") != std::string::npos);
        foreign.sim_rules = 4;
        ASSERT_TRUE(rules_refusal(foreign).find("with simulation rules 4; this build plays simulation rules 5, 6, 7, 8, 9, 10 and 11") != std::string::npos);
    } TEST_END();

    TEST_CASE("RP14.2 A recording of the number 8 or 7, made in code on a map with a named fire wall, plays to every hash it holds and to the final hash that the build before the numbers made of it") {
        bool ok = false;
        const Made eight = make(0, 8, level);
        ASSERT_TRUE(eight.error.empty() && eight.bomber_dead && eight.fire_out && eight.powerups == 1);       // (the fire is out, the Bomber is dead and left its power-up somewhere)
        expect_plays_to(eight, kBefore8, level, 8, 0, ok);
        ASSERT_TRUE(ok);
        const Made seven = make(k187, 7, level);
        ASSERT_TRUE(seven.error.empty() && seven.bomber_dead && seven.fire_out);
        expect_plays_to(seven, kBefore7, level, 7, k187, ok);
        ASSERT_TRUE(ok);
        // written again, the files are the same bytes
        std::string error;
        ASSERT_EQ(encode(eight.replay, error), eight.file);
        ASSERT_EQ(encode(seven.replay, error), seven.file);
    } TEST_END();

    TEST_CASE("RP14.3 The same match recorded under the number 9 or 10 is another match from the first hash (the state hash holds the loader's overlay); the files play to their pinned hashes") {
        bool ok = false;
        const Made nine = make(0, 9, level);
        ASSERT_TRUE(nine.error.empty() && nine.bomber_dead && nine.fire_out && nine.powerups == 1);
        expect_plays_to(nine, kNow9, level, 9, 0, ok);
        ASSERT_TRUE(ok);
        const Made ten = make(k187, 10, level);
        ASSERT_TRUE(ten.error.empty() && ten.bomber_dead && ten.fire_out);
        expect_plays_to(ten, kNow10, level, 10, k187, ok);
        ASSERT_TRUE(ok);
        ASSERT_TRUE(nine.hash != kBefore8.final_hash && ten.hash != kBefore7.final_hash);
        ASSERT_TRUE(nine.replay.hashes[0] != kBefore8.hash100 && ten.replay.hashes[0] != kBefore7.hash100);       // (the state hash holds the overlay: another match from the first hash)
        // the commands are the same: only the loader differs
        const Made eight = make(0, 8, level);
        ASSERT_TRUE(eight.replay.commands.size() == nine.replay.commands.size());
        for (size_t i = 0; i < eight.replay.commands.size(); ++i) {
            ASSERT_EQ(eight.replay.commands[i].turn, nine.replay.commands[i].turn);
            ASSERT_TRUE(eight.replay.commands[i].command.ants == nine.replay.commands[i].command.ants);
        }
    } TEST_END();

    TEST_CASE("RP14.4 A file played under another number of its mode is another match; a number nobody plays is not played; a copy of the engine keeps the loader of its number") {
        const Made eight = make(0, 8, level);
        const Made nine = make(0, 9, level);
        const Outcome nine_as_eight = play(with_rules(nine.replay, 8), level);
        ASSERT_TRUE(!nine_as_eight.ok && nine_as_eight.first_bad_turn > 0u && nine_as_eight.first_bad_turn <= 100u);       // (the first hash that the file holds, after turn 100, is not reached: the loader is another)
        const Outcome eight_as_nine = play(with_rules(eight.replay, 9), level);
        ASSERT_TRUE(!eight_as_nine.ok && eight_as_nine.first_bad_turn > 0u && eight_as_nine.first_bad_turn <= 100u);       // (the first hash that the file holds, after turn 100, is not reached: the loader is another)
        const Made ten = make(k187, 10, level);
        const Outcome ten_as_seven = play(with_rules(ten.replay, 7), level);
        ASSERT_TRUE(!ten_as_seven.ok && ten_as_seven.first_bad_turn > 0u && ten_as_seven.first_bad_turn <= 100u);       // (the first hash that the file holds, after turn 100, is not reached: the loader is another)
        // a number from a newer build: not played, and the refusal says what this build plays
        const Outcome newer = play(with_rules(nine.replay, 12), level);
        ASSERT_FALSE(newer.ran);
        ASSERT_TRUE(newer.error.find("simulation rules 12") != std::string::npos && newer.error.find("5, 6, 7, 8, 9, 10 and 11") != std::string::npos);
        ASSERT_FALSE(play(with_rules(nine.replay, 4), level).ran);
        // 7 and 10 are 187's: with the original's mode they are no file and not played
        Replay mixed = nine.replay;
        mixed.head.sim_rules = 11;
        ASSERT_FALSE(play(mixed, level).ran);
        mixed.head.sim_rules = 9;
        mixed.head.mode = k187;
        ASSERT_FALSE(play(mixed, level).ran);
        // a copy of the engine in the middle of the match: the loader (the overlay in the state) and the play are the same, under the old rules and the new
        for (const bool as_original : {true, false}) {
            sim::SimulationEngine engine;
            engine.set_fire_wall_as_original(as_original);
            engine.init(level, scenario::kSeed, scenario::kRoster);
            uint32_t turn = 0;
            for (; turn < 100; ++turn) {
                for (const sim::Command& c : scenario::orders(engine, turn)) engine.apply_command(c);
                engine.tick();
                engine.clear_news_events();
                engine.clear_audio_events();
            }
            sim::SimulationEngine copy(engine);
            ASSERT_EQ(copy.fire_wall_as_original(), as_original);
            ASSERT_EQ(copy.grid().get_cell(scenario::kWall).is_obstacle_overlay, !as_original);
            for (; turn < 600; ++turn) {
                for (const sim::Command& c : scenario::orders(engine, turn)) {
                    engine.apply_command(c);
                    copy.apply_command(c);
                }
                engine.tick();
                copy.tick();
                engine.clear_news_events();
                engine.clear_audio_events();
                copy.clear_news_events();
                copy.clear_audio_events();
                ASSERT_TRUE(engine.state_hash() == copy.state_hash());
            }
        }
    } TEST_END();

    std::cout << "\n=======================================================\n"
              << " TEST SUMMARY: Total test cases: " << g_test_count << ", Total assertions: " << g_assert_count << ", Failed: " << g_test_failures << "\n"
              << "=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}
