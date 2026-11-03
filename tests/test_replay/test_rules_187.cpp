// Tests of the two rules numbers of the game mode 187 (ants_replay/replay.hpp, docs/REPLAYS.md "Which files play: sim_rules", docs/GAMEPLAY.md "187"): the hill used to work in 187 (rules number 6)
// and is closed now (rules number 7), and this build plays both.
//   RP12.1  the numbers: what a build plays, what a head may say, what a match is recorded under, what a refusal says
//   RP12.2  an old 187 recording (tests/data/replay_187_rules6.antsrep, made by the build before the closed hill, two Hard computer players on TINY) plays to the hashes it holds, byte for byte,
//           and its play really uses the hill (ants enter it and are healed); the same file under the number 7 is another match
//   RP12.3  a 187 match that is played and recorded now has the number 7, plays to its hashes, and no ant ever enters a hill in it
//   RP12.4  the original's game: its number, its reference match's hash and its head are what they were
#include "ants_ai/arena.hpp"
#include "ants_replay/player.hpp"
#include "ants_replay/recorder.hpp"
#include "ants_replay/replay.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_test_paths.hpp"

#include <cstdint>
#include <cstdlib>
#include <fstream>
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

// The recording of rules number 6: made by the build before the closed hill (bot_arena --map TINY --seeds 3..3 --game-mode 187 --ticks 8000 --seat 0=standard:Hard --seat 1=standard:Hard
// --save-replays), played to its end by that build ("replay_tool verify": 7205 turns, 72 hashes and the final state match). Its final hash is pinned here.
constexpr const char* kFixtureName = "replay_187_rules6.antsrep";
constexpr uint64_t kFixtureFinalHash = 0x5b742313037c82a2ull;
constexpr uint32_t kFixtureTurns = 7205;

std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

struct Tiny {
    assets::LevelData level;
    bool ok{false};
    Tiny() { ok = level.load_from_file(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/TINY.LVL"); }
};

struct Seen {
    uint32_t enter_ticks{0};         // ticks in which some ant played the enter clip of its hill
    uint32_t healed{0};              // times an ant's hit points rose from one tick to the next
    uint32_t queued_ticks{0};        // ticks in which some ant waited or walked to the ring of its hill
};

// Plays `replay` and watches the hills: how often an ant entered, how often a hurt ant got better, how often one was queued
Outcome play_watching(const Replay& replay, const assets::LevelData& level, Seen& seen) {
    Hooks hooks;
    std::vector<std::pair<uint32_t, uint16_t>> last;      // (ant id, hp) at the tick before
    hooks.after_tick = [&](uint32_t, sim::SimulationEngine& engine) {
        bool entering = false;
        bool queued = false;
        std::vector<std::pair<uint32_t, uint16_t>> now;
        for (const sim::AntSnapshot& a : engine.get_world_state().ants) {
            if (engine.get_unit(a.id).loco_action == sim::AntUnit::kActionEnter) entering = true;       // (the enter clip of a hill; a newborn plays the hatch clip, which is another action)
            if (engine.is_ant_in_base_queue(a.id)) queued = true;
            now.emplace_back(a.id, a.hp);
            for (const auto& before : last) {
                if (before.first == a.id && a.hp > before.second && a.hp > 0) ++seen.healed;
            }
        }
        if (entering) ++seen.enter_ticks;
        if (queued) ++seen.queued_ticks;
        last.swap(now);
    };
    return play(replay, level, hooks);
}

Replay with_rules(Replay r, uint16_t rules) {
    r.head.sim_rules = rules;
    return r;
}

}  // namespace

int main() {
    std::cout << "=== Rules numbers of game mode 187 ===\n";
    const Tiny tiny;
    if (!tiny.ok) std::cout << "  (cannot load TINY)\n";

    TEST_CASE("RP12.1 This build plays 5 and 8 for the original's game and 6 and 7 for 187, nothing else; a 187 match is recorded under 7; a head may name 6 or 7 only with the mode, and a refusal names all four numbers") {
        ASSERT_TRUE(kSimRulesBeforeLastSide == 5 && kSimRulesMode187Heal == 6 && kSimRulesMode187 == 7 && kSimRules == 8);
        ASSERT_TRUE(sim_rules_for_mode(0) == 8 && sim_rules_for_mode(1) == 7 && sim_rules_for_mode(2) == 0 && sim_rules_for_mode(255) == 0);
        for (uint16_t rules = 0; rules < 12; ++rules) {
            ASSERT_EQ(plays_mode_rules(0, rules), rules == 5 || rules == 8);
            ASSERT_EQ(plays_mode_rules(k187, rules), rules == 6 || rules == 7);
            ASSERT_FALSE(plays_mode_rules(2, rules));
            ASSERT_FALSE(plays_mode_rules(255, rules));
        }
        ASSERT_EQ(played_rules_text(), std::string("5, 6, 7 and 8"));
        // heads: the mode and the number go together
        Header head;
        head.map_name = "TINY.LVL";
        head.roster = 0x0F;
        for (const uint16_t rules : {uint16_t{6}, uint16_t{7}}) {
            Header h = head;
            h.set_mode(k187);
            h.sim_rules = rules;
            Replay r;
            r.head = h;
            std::string error;
            ASSERT_FALSE(encode(r, error).empty());                                          // (the writer takes both numbers with the mode)
            ASSERT_TRUE(plays_here(h));
            h.set_mode(0);
            h.sim_rules = rules;
            r.head = h;
            ASSERT_TRUE(encode(r, error).empty() && !plays_here(h));                         // (the rules of 187 without the mode are no file, and no build plays them as the original's game)
        }
        {
            Header h = head;
            h.set_mode(k187);
            h.sim_rules = 5;
            Replay r;
            r.head = h;
            std::string error;
            ASSERT_TRUE(encode(r, error).empty() && !plays_here(h));                         // (the mode with the original's number: a build that skips field 17 would play it as the original's game)
            h.sim_rules = 8;
            r.head = h;
            ASSERT_TRUE(encode(r, error).empty() && !plays_here(h));                         // (the original's number 8 with the mode, like 5)
            h.sim_rules = 9;
            r.head = h;
            ASSERT_FALSE(encode(r, error).empty());                                          // (a number from a newer build is a file; it is just not played here)
            ASSERT_FALSE(plays_here(h));
            h.sim_rules = 4;
            ASSERT_FALSE(plays_here(h));
        }
        // a match that a Recorder starts is recorded under the number of the rules it is played by: a head that says 5, 6 or 8 for 187 gets 7
        for (const uint16_t said : {uint16_t{5}, uint16_t{6}, uint16_t{7}, uint16_t{8}}) {
            Header h = head;
            h.set_mode(k187);
            h.sim_rules = said;
            Recorder rec(h);
            ASSERT_EQ(rec.replay().head.sim_rules, 7);
        }
        Header refused = head;
        refused.set_mode(k187);
        refused.sim_rules = 9;
        ASSERT_TRUE(rules_refusal(refused).find("simulation rules 5, 6, 7 and 8") != std::string::npos);
    } TEST_END();

    TEST_CASE("RP12.2 An old 187 recording (rules number 6, made by the build before the closed hill) plays to every hash it holds and to its final hash, byte for byte, and its play uses the hill; the same file under the number 7 is another match") {
        const std::vector<uint8_t> bytes = read_file(std::string(TEST_DATA_DIR) + "/" + kFixtureName);
        ASSERT_FALSE(bytes.empty());
        Replay old;
        std::string error;
        ASSERT_TRUE(decode(bytes.data(), bytes.size(), old, error));
        ASSERT_TRUE(old.complete && old.head.mode == k187 && old.head.sim_rules == 6 && sim_rules_of(old.head) == 6);
        ASSERT_TRUE(plays_here(old.head));
        ASSERT_EQ(old.total_turns, kFixtureTurns);
        ASSERT_EQ(old.final_hash, kFixtureFinalHash);
        ASSERT_EQ(old.hashes.size(), 72u);
        ASSERT_TRUE(tiny.ok);
        Seen seen;
        const Outcome o = play_watching(old, tiny.level, seen);
        ASSERT_TRUE(o.ran && o.ok && o.complete);
        ASSERT_EQ(o.hash, kFixtureFinalHash);                                                // the state hash of the engine at the end is the one the old build wrote
        ASSERT_EQ(o.hashes_checked, 72u);
        ASSERT_EQ(o.first_bad_turn, 0u);
        ASSERT_TRUE(seen.enter_ticks > 0 && seen.healed > 0);                                // (the match really uses the hill: ants went in and came out whole)
        // written again, the file is the same bytes (the format is as it was)
        std::vector<uint8_t> again = encode(old, error);
        ASSERT_EQ(again, bytes);
        // the same commands under the number 7 are another match: the closed hill refuses the orders home, so a hash that the file holds is not reached
        const Outcome closed = play(with_rules(old, 7), tiny.level);
        ASSERT_FALSE(closed.ok);
        ASSERT_TRUE(closed.first_bad_turn > 0 && closed.first_bad_turn < kFixtureTurns);                    // (it is named: the first hash that differs)
        // and under the number 5 (a number of the original's game, with the mode of 187) it is not played at all, and as the original's game it is another match
        const Outcome five = play(with_rules(old, 5), tiny.level);
        ASSERT_FALSE(five.ran);
        Replay as_original = old;
        as_original.head.set_mode(0);
        as_original.head.sim_rules = kSimRulesBeforeLastSide;                              // (the mode taken away and the number of the original's game before the rule: it plays, as another match)
        ASSERT_FALSE(play(as_original, tiny.level).ok);
    } TEST_END();

    TEST_CASE("RP12.3 A 187 match that is played and recorded now has the number 7, plays to its hashes and its final hash, and no ant enters a hill in it; the same match under the number 6 is another match") {
        ASSERT_TRUE(tiny.ok);
        // two Hard computer players on TINY for 6000 turns in 187, recorded as a game on one machine records it: the arena's log of commands is fed to a recorder and an engine, step by step
        ai::ArenaSpec spec;
        spec.level = &tiny.level;
        spec.seed = 3;
        spec.max_ticks = 6000;
        spec.game_mode = sim::GameMode::Kills187;
        spec.record = true;
        for (uint8_t seat = 0; seat < 2; ++seat) spec.bots.push_back(ai::BotSpec{seat, "standard", ai::Level::Hard, ai::Style::Random});
        const ai::ArenaResult match = ai::play_match(spec);
        ASSERT_TRUE(match.error.empty() && match.log.size() > 100);
        Header head;
        head.game_version = "v0.0.0";
        head.build_id = "test";
        head.venue = "bot arena";
        head.map_name = "TINY.LVL";
        head.seed = 3;
        head.roster = 0x03;
        head.set_mode(k187);
        Recorder rec(head);
        ASSERT_EQ(rec.replay().head.sim_rules, 7);
        sim::SimulationEngine engine;
        engine.set_game_mode(sim::GameMode::Kills187);
        ASSERT_FALSE(engine.own_hill_enterable());                                           // (a match that is played now has the hill closed without anybody saying so)
        engine.init(tiny.level, 3, 0x03);
        size_t next = 0;
        for (uint64_t step = 0; step < match.steps; ++step) {
            for (; next < match.log.size() && match.log[next].step == step; ++next) {
                engine.apply_command(match.log[next].command);
                rec.on_command(match.log[next].command);
            }
            engine.tick();
            engine.clear_news_events();
            engine.clear_audio_events();
            rec.on_tick(engine);
        }
        ASSERT_EQ(next, match.log.size());
        std::string error;
        const std::vector<uint8_t> file = rec.finish(engine, error);
        ASSERT_FALSE(file.empty());
        Replay r;
        ASSERT_TRUE(decode(file.data(), file.size(), r, error));
        ASSERT_TRUE(r.head.sim_rules == 7 && plays_here(r.head));
        Seen seen;
        const Outcome o = play_watching(r, tiny.level, seen);
        ASSERT_TRUE(o.ran && o.ok);
        ASSERT_EQ(o.hash, engine.state_hash().total);
        ASSERT_TRUE(seen.enter_ticks == 0 && seen.healed == 0 && seen.queued_ticks == 0);   // (nobody went in, nobody got better, nobody waited at a ring)
        const Outcome open = play(with_rules(r, 6), tiny.level);
        ASSERT_FALSE(open.ok);                                                               // (the hill that works is another match)
    } TEST_END();

    TEST_CASE("RP12.4 The original's game is what it was: a head with no field 17 and no 187 number (its number is 8 now, test_rules_last_side), and a mode-0 file that names 6 or 7 is refused") {
        Header head;
        head.map_name = "TINY.LVL";
        head.roster = 0x0F;
        Recorder rec(head);
        ASSERT_EQ(rec.replay().head.sim_rules, 8);
        ASSERT_EQ(rec.replay().head.mode, 0);
        ASSERT_TRUE(plays_here(rec.replay().head));
        for (const uint16_t rules : {uint16_t{6}, uint16_t{7}}) {
            Header h = head;
            h.sim_rules = rules;
            ASSERT_FALSE(plays_here(h));
        }
        sim::SimulationEngine original;
        ASSERT_TRUE(original.own_hill_enterable());
        ASSERT_TRUE(tiny.ok);
        original.init(tiny.level, 3, 0x0F);
        ASSERT_TRUE(original.own_hill_enterable() && original.game_mode() == sim::GameMode::HighestScore);
    } TEST_END();

    std::cout << "\n=======================================================\n"
              << " TEST SUMMARY: Total test cases: " << g_test_count << ", Total assertions: " << g_assert_count << ", Failed: " << g_test_failures << "\n"
              << "=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}
