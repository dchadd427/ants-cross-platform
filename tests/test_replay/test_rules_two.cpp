// Tests of a match on a map of rules 2 (the long-order rules: the wide search of the path finder, docs/MAP_FORMAT.md "Orders on a big map") as a recording, and of the file's word for a
// reader that does not know the word (docs/REPLAYS.md "Which files play: sim_rules"). The map carries the word, so the recording needs no rules number of its own.
//   RP13.1  TINY grown to 512 x 512 (rules 2): an ant sent 450 tiles along its row gets a path (at rules 1 it gets none), the match is recorded under kSimRules, plays to its hashes, and
//           ends in the hash that is pinned (the reference match of a map of rules 2)
//   RP13.2  a reader that does not know the word 2 (modelled by the word 3) refuses the map that a file carries, with the reader's sentence; this build reads the 2
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

// The match on `level` for 400 turns with the orders above, recorded as a game on one machine records it, and played again by the player
Played play_recorded(const assets::LevelData& level, uint64_t map_hash) {
    Played p;
    sim::SimulationEngine engine;
    engine.init(level, 31, 0x0F);
    Header head;
    head.game_version = "v0.0.0";
    head.build_id = "test";
    head.venue = "local game";
    head.map_name = "BIG.LVL";
    head.map_hash = map_hash;
    head.seed = 31;
    head.roster = 0x0F;
    Recorder rec(head);
    for (uint32_t turn = 0; turn < 400; ++turn) {
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

    TEST_CASE("RP13.1 TINY Grown To 512 x 512 Is A Map Of Rules 2: An Ant Sent 450 Tiles Along Its Row Gets A Path (At Rules 1 It Gets None), The Match Is Recorded Under kSimRules, Plays To Its Hashes And Ends In The Pinned Hash") {
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

    TEST_CASE("RP13.2 A Reader That Does Not Know The Word 2 (The Word 3 Stands For It) Refuses The Map That A File Carries With The Reader's Sentence; This Build Reads The Word 2 From The File") {
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

    std::cout << "\n=======================================================\n"
              << " TEST SUMMARY: Total test cases: " << g_test_count << ", Total assertions: " << g_assert_count << ", Failed: " << g_test_failures << "\n"
              << "=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}
