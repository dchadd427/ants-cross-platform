#pragma once

// The reference numbers of the worker bot: the procedure that produces the table pinned in tests/test_ai/baselines.inc, in ONE place, so that the test that checks the table
// and the command that writes it (`bot_arena --write-baselines`) can never measure different things.
//
// For one map and one level:
//   solo_2min[s]   the worker bot alone on seat s against three idle bots, played for 2 minutes (2400 ticks): the speed of the first claims
//   solo_full[s]   the same for the whole match: the bot's economy with nobody in the way (the hill gate binds)
//   four_full[s]   four worker bots of the level, one on every seat, the whole match: contested, scores are shares of a finite pot
//   four_sum       the sum of the four scores: the whole reachable pot, but for the points that ants carry when the clock runs out (below) and a unit that two hills bite in the
//                  same moment (above)
//   pot            the points that some hill can walk to (MapInfo::reachable_points)
// Every number is the mean over kBaselineSeeds (the seed changes the facing of ants and what a dropper drops, a little). Seats are ROTATED because they are not symmetric on a
// map (a hill's distance to the food differs), and commands reach the engine like in a room (the arena's sink latency, kBaselineLatency). Since v0.1.1 the bots open like
// the bots of a real match (ArenaSpec::start_hold: a bucket of one token and, since v0.2.0, a first look on tick 1 + the seat: the match clock waits for the "Get ready to play!" dialog in the game, so the
// arena's tick 0 is the game's), and the table was regenerated for it.
//
// The numbers depend on how the hill banks deposits (about one every 100 ticks) and on the bot: when either changes on purpose, the table is regenerated on purpose:
//   cmake --build build --target bot_arena && ./build/bot_arena --write-baselines > tests/test_ai/baselines.inc
//
// ISLANDS: no hill can walk to any food, so the worker bot scores 0 there (0 = 0, exempt until the island hops of milestone B4a).

#include <array>
#include <cstdint>
#include <string>

#include "ants_ai/bot.hpp"
#include "ants_assets/lvl_parser.hpp"

namespace ants::ai {

inline constexpr uint32_t kBaselineSeeds[] = {1, 2};
inline constexpr uint64_t kBaselineShortTicks = 2400;
inline constexpr uint32_t kBaselineLatency = 3;

struct BaselineRow {
    std::string map;                         // "TINY": the shipped maps' names
    Level level{Level::Medium};
    std::array<int32_t, 4> solo_2min{};
    std::array<int32_t, 4> solo_full{};
    std::array<int32_t, 4> four_full{};
    int32_t four_sum{0};
    int32_t pot{0};
    /// A match that could not be played (never for a shipped map): not empty = the row is not valid
    std::string error;
};

/// Plays the matches of the procedure for the map and the level (sequentially; the tool spreads maps and levels over threads). `kind` is the bot kind: "worker".
BaselineRow measure_baseline(const assets::LevelData& map, const std::string& map_name, Level level, const std::string& kind = "worker");

/// A row as the line of baselines.inc: `{"TINY", Level::Medium, {...}, {...}, {...}, sum, pot},`
std::string baseline_line(const BaselineRow& row);

}  // namespace ants::ai
