// What a look of the standard bot costs (B4-1, AI13.1, acceptance A6): the time of one look (BotView::build + think) at the three levels on every shipped map, in the middle of a match with
// four bots, next to the worker's. A measurement with a loose bound (the machine of a test run is shared and busy); the numbers are printed and are the ones of docs/BOTS.md ("Cost").
//
//   AI13.1  the cost of a look: the worker's and the standard bot's, at every level, on every shipped map after 3,000 ticks of a match
#include "ai_test.hpp"
#include "b41_helpers.hpp"

#include <chrono>

#include "ants_ai/arena.hpp"
#include "ants_ai/worker_bot.hpp"

using namespace ai_test;
using namespace ants;
using namespace ants::ai;
using namespace b41;

namespace {

struct LookCost {
    double build_us{0};      // BotView::build
    double think_us{0};      // think on a view (the bot's memory is warm after the first looks)
};

// The mean time of a look of `bot` at `seat` on the engine as it stands: 60 warm-up looks, then 400 timed ones
LookCost cost_of(const sim::SimulationEngine& sim, uint8_t seat, Bot& bot, const MapInfo& map, const Profile& profile) {
    bot.start(BotContext{seat, profile, 7u, &map});
    LookCost out;
    for (int i = 0; i < 60; ++i) {
        const BotView view = BotView::build(sim, seat, &map);
        Orders orders;
        bot.think(view, orders);
    }
    using Clock = std::chrono::steady_clock;
    const int looks = 400;
    double build = 0;
    double think = 0;
    for (int i = 0; i < looks; ++i) {
        const auto t0 = Clock::now();
        const BotView view = BotView::build(sim, seat, &map);
        const auto t1 = Clock::now();
        Orders orders;
        bot.think(view, orders);
        const auto t2 = Clock::now();
        build += std::chrono::duration<double, std::micro>(t1 - t0).count();
        think += std::chrono::duration<double, std::micro>(t2 - t1).count();
    }
    out.build_us = build / looks;
    out.think_us = think / looks;
    return out;
}

}  // namespace

void run_b41_cost_tests() {
    TEST_CASE("AI13.1 The Cost Of A Look (Measured): BotView::build Plus think, At Easy, Medium And Hard, On Every Shipped Map In The Middle Of A Match Of Four Standard Bots, Next To The Worker's; A Look Costs Well Under A Millisecond (The Bound Is 5 ms: The Machine Is Shared)")
    {
        static const char* const kMaps[] = {"TINY", "SMALL", "MEDIUM", "GAUNTLET", "TREASURE", "ISLANDS"};
        std::cout << "\n    [microseconds per look, BotView::build + think, seat 0, after 3,000 ticks of four standard bots at the level; the worker at the same level in brackets]";
        for (const char* map_name : kMaps) {
            assets::LevelData level_data;
            ASSERT_TRUE(level_data.load_from_file(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/" + map_name + ".LVL"));
            std::cout << "\n    " << std::left << std::setw(9) << map_name;
            for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
                ArenaSpec spec;
                spec.level = &level_data;
                spec.seed = 3;
                spec.max_ticks = 3000;
                for (uint8_t seat = 0; seat < 4; ++seat) {
                    BotSpec b;
                    b.seat = seat;
                    b.kind = "standard";
                    b.level = level;
                    spec.bots.push_back(b);
                }
                LookCost standard;
                LookCost worker;
                spec.inspect = [&](const sim::SimulationEngine& sim) {
                    const MapInfo map(sim);
                    Profile profile = profile_for(level);
                    StandardBot s(level, Style::Random);
                    standard = cost_of(sim, 0, s, map, profile);
                    WorkerBot w;
                    worker = cost_of(sim, 0, w, map, profile);
                };
                const ArenaResult r = play_match(spec);
                ASSERT_TRUE(r.error.empty());
                std::cout << std::right << " | " << level_name(level)[0] << " " << std::fixed << std::setprecision(1) << std::setw(6) << standard.build_us + standard.think_us << " (" << std::setw(5) << worker.build_us + worker.think_us << ")";
                ASSERT_TRUE(standard.build_us + standard.think_us < 5000.0);                                  // under 5 ms: a look every 4 ticks (200 ms) at Hard is a few thousandths of the time
                ASSERT_TRUE(worker.build_us + worker.think_us < 5000.0);
            }
        }
        // the whole of a match: four bots of a kind on TREASURE for the full length, the wall time of the match (the engine, the controller, the views and the bots: the engine's share is the idle
        // bots' row)
        assets::LevelData treasure;
        ASSERT_TRUE(treasure.load_from_file(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/TREASURE.LVL"));
        std::cout << "\n    [wall milliseconds of a whole TREASURE match (14,400 ticks), four bots of one kind: idle / worker / standard]";
        const auto match_ms = [&](const char* kind, Level level) {
            ArenaSpec spec;
            spec.level = &treasure;
            spec.seed = 4;
            for (uint8_t seat = 0; seat < 4; ++seat) {
                BotSpec b;
                b.seat = seat;
                b.kind = kind;
                b.level = level;
                spec.bots.push_back(b);
            }
            const auto t0 = std::chrono::steady_clock::now();
            const ArenaResult r = play_match(spec);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            return r.error.empty() ? ms : -1.0;
        };
        std::cout << "\n    " << std::left << std::setw(9) << "TREASURE";
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            const double idle = match_ms("idle", level);
            const double worker = match_ms("worker", level);
            const double standard = match_ms("standard", level);
            ASSERT_TRUE(idle > 0 && worker > 0 && standard > 0);
            std::cout << std::right << " | " << level_name(level)[0] << " " << std::fixed << std::setprecision(0) << std::setw(5) << idle << " / " << std::setw(5) << worker << " / " << std::setw(5) << standard;
            ASSERT_TRUE(standard < 60000.0);                                                              // a whole match of four bots in under a minute, even on a loaded machine
        }
        std::cout << "\n    ";
    } TEST_END();
}
