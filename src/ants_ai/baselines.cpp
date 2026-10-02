#include "ants_ai/baselines.hpp"

#include <vector>

#include "ants_ai/arena.hpp"
#include "ants_ai/map_info.hpp"
#include "ants_sim/sim_engine.hpp"

namespace ants::ai {

namespace {

int32_t mean_of(int64_t sum, size_t n) { return n == 0 ? 0 : static_cast<int32_t>((sum + static_cast<int64_t>(n) / 2) / static_cast<int64_t>(n)); }

const char* level_word(Level level) {
    switch (level) {
        case Level::Easy: return "Easy";
        case Level::Medium: return "Medium";
        case Level::Hard: return "Hard";
    }
    return "Medium";
}

std::string list4(const std::array<int32_t, 4>& v) {
    return "{" + std::to_string(v[0]) + ", " + std::to_string(v[1]) + ", " + std::to_string(v[2]) + ", " + std::to_string(v[3]) + "}";
}

}  // namespace

BaselineRow measure_baseline(const assets::LevelData& map, const std::string& map_name, Level level, const std::string& kind) {
    BaselineRow row;
    row.map = map_name;
    row.level = level;
    {
        sim::SimulationEngine engine;
        engine.init(map, 1, 0x0F);
        row.pot = static_cast<int32_t>(MapInfo(engine).reachable_points());
    }
    const size_t seeds = sizeof(kBaselineSeeds) / sizeof(kBaselineSeeds[0]);
    std::array<int64_t, 4> solo_short{};
    std::array<int64_t, 4> solo_full{};
    std::array<int64_t, 4> four{};
    int64_t four_sum = 0;
    const auto bot_at = [&](uint8_t seat, const std::string& of_kind) {
        BotSpec b;
        b.seat = seat;
        b.kind = of_kind;
        b.level = level;
        return b;
    };
    for (const uint32_t seed : kBaselineSeeds) {
        for (uint8_t mine = 0; mine < 4; ++mine) {
            for (const uint64_t ticks : {kBaselineShortTicks, uint64_t{0}}) {
                ArenaSpec s;
                s.level = &map;
                s.seed = seed;
                s.max_ticks = ticks;
                s.latency_ticks = kBaselineLatency;
                for (uint8_t seat = 0; seat < 4; ++seat) s.bots.push_back(bot_at(seat, seat == mine ? kind : std::string("idle")));
                const ArenaResult r = play_match(s);
                if (!r.error.empty()) {
                    row.error = r.error;
                    return row;
                }
                (ticks == 0 ? solo_full : solo_short)[mine] += r.seats[mine].score;
            }
        }
        ArenaSpec s;
        s.level = &map;
        s.seed = seed;
        s.max_ticks = 0;
        s.latency_ticks = kBaselineLatency;
        for (uint8_t seat = 0; seat < 4; ++seat) s.bots.push_back(bot_at(seat, kind));
        const ArenaResult r = play_match(s);
        if (!r.error.empty()) {
            row.error = r.error;
            return row;
        }
        for (uint8_t seat = 0; seat < 4; ++seat) {
            four[seat] += r.seats[seat].score;
            four_sum += r.seats[seat].score;
        }
    }
    for (size_t seat = 0; seat < 4; ++seat) {
        row.solo_2min[seat] = mean_of(solo_short[seat], seeds);
        row.solo_full[seat] = mean_of(solo_full[seat], seeds);
        row.four_full[seat] = mean_of(four[seat], seeds);
    }
    row.four_sum = mean_of(four_sum, seeds);
    return row;
}

std::string baseline_line(const BaselineRow& row) {
    return "{\"" + row.map + "\", Level::" + level_word(row.level) + ", " + list4(row.solo_2min) + ", " + list4(row.solo_full) + ", " + list4(row.four_full) + ", " +
           std::to_string(row.four_sum) + ", " + std::to_string(row.pot) + "},";
}

}  // namespace ants::ai
