// Tests of a match on a map of rules 2 (the long-order rules: the wide search of the path finder, docs/MAP_FORMAT.md "Orders on a big map") as a recording, and of the file's word for a
// reader that does not know the word (docs/REPLAYS.md "Which files play: sim_rules"). The map carries the word, so the recording needs no rules number of its own.
//   RP16.1  TINY grown to 512 x 512 (rules 2): an ant sent 450 tiles along its row gets a path (at rules 1 it gets none), the match is recorded under kSimRules, plays to its hashes, and
//           ends in the hash that is pinned (the reference match of a map of rules 2)
//   RP16.2  a reader that does not know the word 2 (modelled by the word 3) refuses the map that a file carries, with the reader's sentence; this build reads the 2
//   RP16.3  the word takes no rules number of its own: the same match plays to the same hashes under every number that this build plays (5, 8 and 9 for the original's game, 6, 7, 10 and 11 for 187), and the number after the newest is refused
#include "ants_replay/player.hpp"
#include "ants_replay/recorder.hpp"
#include "ants_replay/replay.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_test_paths.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>
#include "../common/test_assert.hpp"
#include "../test_server/big_map_fixtures.hpp"

using namespace ants;
using namespace ants::replay;
using ants::sim::Command;
using ants::sim::CommandType;

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

constexpr uint64_t kReferenceHash = 0x1bfa58fee340fb19ull;   // the end of the reference match below (a new map of 512 x 512, rules 2)

// At turn 5 one ant of each seat is sent along its row to column 480, 450 tiles and more away (the original's search gives up at 400 on open grass)
std::vector<Command> long_orders_at(const sim::SimulationEngine& engine, uint32_t turn) {
    std::vector<Command> out;
    if (turn != 5) return out;
    for (uint8_t seat = 0; seat < 4; ++seat) {
        for (const sim::AntSnapshot& a : engine.get_world_state().ants) {
            if (a.player_id != seat || !(a.hp > 0)) continue;
            Command c;
            c.type = CommandType::GroupMove;
            c.issuer = seat;
            c.tile_x = 480;
            c.tile_y = static_cast<int16_t>(a.tile_y);
            c.ants = {a.id};
            out.push_back(c);
            break;
        }
    }
    return out;
}

struct Played {
    Replay replay;
    uint64_t hash{0};
    size_t longest_path{0};           // the longest path that any ant held while the recorded match was played again
    bool verified{false};
};

// The match on `level` with the orders above, recorded as a game on one machine records it, and played again by the player: 400 turns in the original's game (under the number of now), 150 in 187 (mode 1: the path
// of the 450-tile order is found at about turn 55, and the hash of the state holds it). For 187 `number` is the rules number to record under: the engine is set up as the player of a file of that number sets it up
// (begin_match), and the file says that number, as a build of it wrote it (the recorder stamps the number of now).
Played play_recorded(const assets::LevelData& level, uint64_t map_hash, uint8_t mode = 0, uint16_t number = 0) {
    Played p;
    sim::SimulationEngine engine;
    Header head;
    if (mode != 0) head.set_mode(mode);
    if (mode != 0 && number != 0) head.sim_rules = number;
    head.game_version = "v0.0.0";
    head.build_id = "test";
    head.venue = "local game";
    head.map_name = "BIG.LVL";
    head.map_hash = map_hash;
    head.seed = 31;
    head.roster = 0x0F;
    if (mode == 0) {
        engine.init(level, 31, 0x0F);
    } else {
        Replay shell;
        shell.head = head;
        begin_match(engine, shell, level, false);
    }
    Recorder rec(head);
    for (uint32_t turn = 0; turn < (mode == 0 ? 400u : 150u); ++turn) {
        std::vector<Command> commands = long_orders_at(engine, turn);
        sim::canonical_order(commands);
        for (const Command& c : commands) {
            engine.apply_command(c);
            rec.on_command(c);
        }
        engine.tick();
        engine.clear_news_events();
        engine.clear_audio_events();
        rec.on_tick(engine);
    }
    p.hash = engine.state_hash().total;
    std::string error;
    const std::vector<uint8_t> file = rec.finish(engine, error);
    Replay decoded;
    if (file.empty() || !decode(file.data(), file.size(), decoded, error)) return p;
    p.replay = std::move(decoded);
    if (mode != 0 && number != 0) p.replay.head.sim_rules = number;
    Hooks hooks;
    hooks.after_tick = [&](uint32_t, sim::SimulationEngine& e) {
        for (const sim::AntSnapshot& a : e.get_world_state().ants) p.longest_path = std::max(p.longest_path, e.get_unit(a.id).waypoints.size());
    };
    const Outcome o = play(p.replay, level, hooks);
    p.verified = o.ran && o.ok && o.complete && o.hash == p.hash;
    return p;
}

}  // namespace

int main() {
    std::cout << "=== A match on a map of rules 2 ===\n";
    const std::string big = grown_tiny(512, 512);
    assets::LevelData two;
    const bool loaded = !big.empty() && two.load_from_memory(reinterpret_cast<const uint8_t*>(big.data()), big.size(), nullptr, assets::LevelLimits::for_play_extended());
    if (!loaded) std::cout << "  (cannot make the map)\n";

    TEST_CASE("RP16.1 TINY Grown To 512 x 512 Is A Map Of Rules 2: An Ant Sent 450 Tiles Along Its Row Gets A Path (At Rules 1 It Gets None), The Match Is Recorded Under kSimRules, Plays To Its Hashes And Ends In The Pinned Hash") {
        ASSERT_TRUE(loaded && two.rules == assets::LVL_RULES_LONG_ORDERS);
        const uint64_t map_hash = net::hash_bytes(reinterpret_cast<const uint8_t*>(big.data()), big.size());
        assets::LevelData one = two;
        one.rules = assets::LVL_RULES_EXTENDED;
        const Played wide = play_recorded(two, map_hash);
        const Played original = play_recorded(one, map_hash);
        ASSERT_TRUE(wide.verified && original.verified);
        ASSERT_EQ(wide.replay.head.sim_rules, kSimRules);                       // (the file says the build's rules number: the map's word is in the map)
        ASSERT_TRUE(wide.longest_path >= 450);                                  // rules 2: the wide search found 450 tiles and more
        ASSERT_TRUE(original.longest_path < 100);                               // rules 1: the original's search gives up at 400 tiles of grass and the ant stays
        ASSERT_TRUE(wide.hash != original.hash);
        if (wide.hash != kReferenceHash) std::cout << "\n    the rules-2 reference match ends in " << std::hex << wide.hash << std::dec << "\n";
        ASSERT_EQ(wide.hash, kReferenceHash);                                   // the reference match of a map of rules 2: the wide search cannot change without this number
    } TEST_END();

    TEST_CASE("RP16.2 A Reader That Does Not Know The Word 2 (The Word 3 Stands For It) Refuses The Map That A File Carries With The Reader's Sentence; This Build Reads The Word 2 From The File") {
        const std::string small = grown_tiny(257, 257);                         // (a map that a file can carry: 66,049 cells, rules 2 as made)
        ASSERT_FALSE(small.empty());
        ASSERT_TRUE(static_cast<uint8_t>(small[14]) == 2);
        Replay file;
        std::string error;
        assets::LevelData read;
        file.embedded_map.assign(small.begin(), small.end());
        file.head.map_hash = net::hash_bytes(file.embedded_map.data(), file.embedded_map.size());
        ASSERT_TRUE(load_embedded_map(file, read, error) && read.rules == assets::LVL_RULES_LONG_ORDERS);
        file.embedded_map[14] = 3;
        file.head.map_hash = net::hash_bytes(file.embedded_map.data(), file.embedded_map.size());
        ASSERT_FALSE(load_embedded_map(file, read, error));
        ASSERT_TRUE(error == "the map in the replay cannot be loaded: the rules word is 3: only 0 (the original's rules), 1 (extended rules) and 2 (extended rules with long orders) are known, the file is not read");
    } TEST_END();

    TEST_CASE("RP16.3 The Word 2 Takes No Rules Number: The Same Match Plays To The Same Hashes Under Every Number That This Build Plays (5, 8 And 9 Of The Original's Game; 7, 10 And 11 Of 187, And 6, Whose Hill Works, Has Its Own), And The Number After The Newest Is Refused") {
        ASSERT_TRUE(loaded);
        const uint64_t map_hash = net::hash_bytes(reinterpret_cast<const uint8_t*>(big.data()), big.size());
        const Played wide = play_recorded(two, map_hash);
        ASSERT_TRUE(wide.verified);
        for (const uint16_t number : {kSimRulesBeforeLastSide, kSimRulesBeforeFireWall, kSimRules}) {       // (TINY has no fire wall and all four seats stay: the three loaders and end rules play this match alike)
            ASSERT_TRUE(plays_mode_rules(0, number));
            Replay copy = wide.replay;
            copy.head.sim_rules = number;
            const Outcome o = play(copy, two, Hooks{});
            ASSERT_TRUE(o.ran && o.ok && o.complete && o.hash == wide.hash);
        }
        // 187 (150 turns): the same orders are another match (the mode is hashed). The numbers 7, 10 and 11 (the hill is closed; the loader and the end rule differ only where there is a named fire wall, a seat without a team or eggs
        // that cannot hatch) play it alike; the number 6 has the hill that works, which the state hash carries, so a match recorded under 6 is another match, and plays to its own hashes under 6.
        constexpr uint8_t k187 = static_cast<uint8_t>(sim::GameMode::Kills187);
        const Played with_hill_closed = play_recorded(two, map_hash, k187);
        ASSERT_TRUE(with_hill_closed.verified);
        ASSERT_EQ(with_hill_closed.replay.head.sim_rules, kSimRulesMode187);
        for (const uint16_t number : {kSimRulesMode187BeforeFireWall, kSimRulesMode187BeforeEndFix, kSimRulesMode187}) {
            ASSERT_TRUE(plays_mode_rules(k187, number));
            Replay copy = with_hill_closed.replay;
            copy.head.sim_rules = number;
            const Outcome o = play(copy, two, Hooks{});
            ASSERT_TRUE(o.ran && o.ok && o.complete && o.hash == with_hill_closed.hash);
        }
        ASSERT_TRUE(plays_mode_rules(k187, kSimRulesMode187Heal));
        const Played with_hill_open = play_recorded(two, map_hash, k187, kSimRulesMode187Heal);
        ASSERT_TRUE(with_hill_open.verified && with_hill_open.hash != with_hill_closed.hash);
        ASSERT_EQ(with_hill_open.replay.head.sim_rules, kSimRulesMode187Heal);
        ASSERT_FALSE(plays_mode_rules(0, static_cast<uint16_t>(kSimRulesNewest + 1)));
        ASSERT_FALSE(plays_mode_rules(k187, static_cast<uint16_t>(kSimRulesNewest + 1)));
        Replay unknown = wide.replay;
        unknown.head.sim_rules = static_cast<uint16_t>(kSimRulesNewest + 1);
        ASSERT_FALSE(plays_here(unknown.head));
    } TEST_END();

    std::cout << "\n=======================================================\n"
              << " TEST SUMMARY: Total test cases: " << g_test_count << ", Total assertions: " << g_assert_count << ", Failed: " << g_test_failures << "\n"
              << "=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}
