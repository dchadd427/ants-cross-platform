// AI31: a game of this computer on an Extended map taller than 90 rows (docs/MAP_FORMAT.md, "Version 9"; docs/TESTING.md). The map is made by tests/common/crowd_level.hpp: 100 x 100 cells of
// open ground, rules 1, one hill for each team near its corner, 25 Combat Ants of each team mixed in the middle. Four standard bots play it.
//
//   AI31.1  the map is an Extended map of rules 1; the game's limits (a game of this computer) read it, a match of the network reads Classic maps only and says so in the one plain sentence
//   AI31.2  the engine makes the world of it: 100 Combat Ants, four hills, open ground everywhere else, rules 1
//   AI31.3  four bots play 2,400 ticks TWICE to the very same hash; the ants fight (kills), the commands replay into a fresh engine to the same hashes; the failed-path counts and the speed are printed
//   AI31.4  the same match on a map that is 120 columns wide and one that is 128 rows tall (the largest the game plays); one column or row more is refused by the game's limits
#include <array>
#include <chrono>
#include <iostream>
#include <string>
#include <utility>
#include <cstdlib>

#include "ai_test.hpp"
#include "ants_ai/arena.hpp"
#include "crowd_level.hpp"

using namespace ai_test;
using namespace ants;
using namespace ants::ai;

namespace {

ArenaSpec four_bots(const assets::LevelData& level, uint32_t seed, uint64_t ticks) {
    ArenaSpec s;
    s.level = &level;
    s.limits = assets::LevelLimits::for_game(true);                                                      // (a game of this computer)
    s.seed = seed;
    s.max_ticks = ticks;
    s.latency_ticks = 3;
    s.record = true;
    for (uint8_t seat = 0; seat < 4; ++seat) {
        BotSpec b;
        b.seat = seat;
        b.kind = "standard";
        b.level = Level::Medium;
        b.style = Style::Aggressive;
        s.bots.push_back(b);
    }
    return s;
}

uint32_t total(const ArenaResult& r, uint32_t ArenaSeatResult::*field) {
    uint32_t n = 0;
    for (const ArenaSeatResult& s : r.seats) n += s.*field;
    return n;
}

}  // namespace

void run_extended_tests() {
    TEST_CASE("AI31.1 The 100 x 100 Map Is An Extended Map Of Rules 1: A Game Of This Computer Reads It, A Match Of The Network Says That Extended Maps Are Not Played Online Yet") {
        const assets::LevelData level = ants_test::crowd_level();
        ASSERT_TRUE(level.version == assets::LVL_EXTENDED_VERSION && level.rules == assets::LVL_RULES_EXTENDED && level.width == 100u && level.height == 100u);
        ASSERT_EQ(level.anthill_spawns.size(), size_t{100});
        uint32_t per_team[4] = {0, 0, 0, 0};
        for (const assets::AnthillSpawn& s : level.anthill_spawns) ++per_team[s.team_id];
        ASSERT_TRUE(per_team[0] == 25 && per_team[1] == 25 && per_team[2] == 25 && per_team[3] == 25);
        const std::string bytes = ants_test::crowd_bytes(level);
        ASSERT_FALSE(bytes.empty());
        if (const char* out = std::getenv("ANTS_CROWD_FILE")) ASSERT_TRUE(ants_test::write_crowd_file(out, level));      // (the file for the hand proofs of docs/TESTING.md: bot_arena --extended, --map, the browser)
        const auto* data = reinterpret_cast<const uint8_t*>(bytes.data());

        assets::LevelData played;
        assets::LevelValidation verdict;
        ASSERT_TRUE(played.load_from_memory(data, bytes.size(), &verdict, assets::LevelLimits::for_game(true)) && verdict.playable);
        const assets::LevelWriteResult again = assets::write_level(played, assets::LevelLimits::for_extended_maps());
        ASSERT_TRUE(again.ok && std::string(again.bytes.begin(), again.bytes.end()) == bytes);        // (what the game reads is the map that was written)
        ASSERT_TRUE(played.validate(0x0F, assets::LevelLimits::for_game(true)).playable);
        ASSERT_FALSE(played.validate(0x0F).playable);                                                    // (judged as a room judges it: Classic maps only)

        assets::LevelData online;
        assets::LevelValidation refused;
        ASSERT_FALSE(online.load_from_memory(data, bytes.size(), &refused, assets::LevelLimits::for_game(false)));
        ASSERT_TRUE(refused.has(assets::LevelProblemKind::VersionNotSupported));
        ASSERT_EQ(refused.online_reason(), std::string("Extended maps can't be played online yet"));
        ASSERT_TRUE(refused.reason() != refused.online_reason());                                       // (the parser's own sentence is the long one)
        assets::LevelData classic_default;
        ASSERT_FALSE(classic_default.load_from_memory(data, bytes.size(), nullptr, assets::LevelLimits{}));
    } TEST_END();

    TEST_CASE("AI31.2 The Engine Makes The World Of The 100 x 100 Map: 100 Combat Ants, Four Hills, Open Ground, Rules 1") {
        const assets::LevelData level = ants_test::crowd_level();
        sim::SimulationEngine sim;
        sim.init(level, 7, 0x0F);
        ASSERT_EQ(sim.grid().width(), 100u);
        ASSERT_EQ(sim.grid().height(), 100u);
        ASSERT_EQ(sim.grid().rules(), assets::LVL_RULES_EXTENDED);
        ASSERT_EQ(sim.get_world_state().ants.size(), size_t{100});
        uint32_t mine[4] = {0, 0, 0, 0};
        for (const auto& a : sim.get_world_state().ants) {
            ASSERT_TRUE(a.player_id < 4);
            ++mine[a.player_id];
        }
        ASSERT_TRUE(mine[0] == 25 && mine[1] == 25 && mine[2] == 25 && mine[3] == 25);
        ASSERT_EQ(sim.grid().anthills().size(), size_t{4});
        const std::array<ants_test::CrowdHill, 4> hills = ants_test::crowd_hills(100, 100);
        uint32_t blocked_outside = 0;                                                                   // (cells that an ant cannot stand on, outside the 4 x 4 blocks of the hills; the top row of every map is solid)
        for (int32_t y = 0; y < 100; ++y) {
            for (int32_t x = 0; x < 100; ++x) {
                bool in_hill = false;
                for (const ants_test::CrowdHill& h : hills) in_hill = in_hill || (static_cast<uint32_t>(y) >= h.row && static_cast<uint32_t>(y) < h.row + 4 && static_cast<uint32_t>(x) >= h.column && static_cast<uint32_t>(x) < h.column + 4);
                const sim::TileCell& cell = sim.grid().get_cell(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
                if (y > 0 && !in_hill && (cell.static_solid || cell.terrain_type != sim::TERRAIN_WALKABLE)) {
                    if (++blocked_outside <= 3) std::cout << "\n    cell " << x << "," << y << " solid " << cell.static_solid << " terrain " << static_cast<int>(cell.terrain_type) << " tile " << cell.terrain_id;
                }
            }
        }
        if (blocked_outside != 0) std::cout << "\n    blocked outside the hills: " << blocked_outside << "\n";
        ASSERT_EQ(blocked_outside, 0u);
    } TEST_END();

    TEST_CASE("AI31.3 Four Bots Play 2,400 Ticks Twice On The 100 x 100 Map To The Very Same Hash; The Ants Fight; The Commands Replay To The Same Hashes") {
        const assets::LevelData level = ants_test::crowd_level();
        const ArenaSpec spec = four_bots(level, 2024, 2400);
        const auto t0 = std::chrono::steady_clock::now();
        const ArenaResult a = play_match(spec);
        const auto t1 = std::chrono::steady_clock::now();
        const ArenaResult b = play_match(spec);
        if (!a.error.empty()) std::cout << "\n    error: " << a.error << "\n";
        ASSERT_TRUE(a.error.empty() && b.error.empty());
        ASSERT_EQ(a.ticks, uint64_t{2400});
        ASSERT_EQ(a.hash, b.hash);
        ASSERT_TRUE(a.checkpoints == b.checkpoints && a.checkpoints.size() == 120);
        ASSERT_EQ(a.log.size(), b.log.size());
        const uint32_t kills = total(a, &ArenaSeatResult::kills);
        const uint32_t losses = total(a, &ArenaSeatResult::losses);
        ASSERT_TRUE(kills > 0 && losses > 0);
        const ReplayResult replay = replay_commands(spec, a);
        ASSERT_TRUE(replay.ok && replay.hash == a.hash);
        // what a person sees of the walks: the "Can't go there." reactions, the orders that were followed by one, the orders in all
        const uint32_t cantgo = total(a, &ArenaSeatResult::cantgo), refused = total(a, &ArenaSeatResult::refused_orders), orders = total(a, &ArenaSeatResult::orders);
        const double seconds = std::chrono::duration<double>(t1 - t0).count();
        std::cout << "\n    [AI31.3] 100 x 100, rules 1, 4 bots: hash " << std::hex << a.hash << std::dec << ", kills " << kills << ", losses " << losses << ", orders " << orders
                  << ", can't-go reactions " << cantgo << ", orders refused " << refused << ", ants left";
        for (const ArenaSeatResult& s : a.seats) std::cout << " " << s.ants;
        std::cout << "; " << a.ticks << " ticks in " << seconds << " s (" << static_cast<uint64_t>(static_cast<double>(a.ticks) / seconds) << " ticks/s, 20 are real time)\n    ";
        ASSERT_TRUE(orders > 0);
        ASSERT_TRUE(cantgo <= orders * 4u + 200u);                                                      // (a bound, not a promise: the walks across the map do not end in a loop of refusals)
    } TEST_END();

    TEST_CASE("AI31.4 The Largest Maps The Game Plays (120 Columns, 128 Rows) Run The Same Way; One Column Or Row More Is Not Read By The Game's Limits") {
        for (const auto& shape : {std::pair<uint32_t, uint32_t>{100, 120}, std::pair<uint32_t, uint32_t>{128, 100}, std::pair<uint32_t, uint32_t>{128, 120}}) {
            const assets::LevelData level = ants_test::crowd_level(shape.first, shape.second);
            const std::string bytes = ants_test::crowd_bytes(level);
            ASSERT_FALSE(bytes.empty());
            assets::LevelData loaded;
            ASSERT_TRUE(loaded.load_from_memory(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), nullptr, assets::LevelLimits::for_game(true)));
            const ArenaSpec spec = four_bots(loaded, 5, 600);
            const ArenaResult one = play_match(spec);
            const ArenaResult two = play_match(spec);
            ASSERT_TRUE(one.error.empty() && one.ticks == 600 && one.hash == two.hash && one.hash != 0);
            ASSERT_TRUE(replay_commands(spec, one).ok);
        }
        for (const auto& shape : {std::pair<uint32_t, uint32_t>{100, 121}, std::pair<uint32_t, uint32_t>{129, 100}}) {
            const assets::LevelData level = ants_test::crowd_level(shape.first, shape.second);
            const std::string bytes = ants_test::crowd_bytes(level);
            ASSERT_FALSE(bytes.empty());                                                                 // (a map that the editor can make and keep)
            assets::LevelData loaded;
            ASSERT_FALSE(loaded.load_from_memory(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), nullptr, assets::LevelLimits::for_game(true)));
        }
    } TEST_END();
}
