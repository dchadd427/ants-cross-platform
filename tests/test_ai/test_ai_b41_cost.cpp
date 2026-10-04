// What a look of the standard bot costs (B4-1, AI13.1, acceptance A6): the time of one look (BotView::build + think) at the three levels on every shipped map, in the middle of a match with
// four bots, next to the worker's. A measurement with a loose bound (the machine of a test run is shared and busy); the numbers are printed and are the ones of docs/BOTS.md ("Cost").
//
//   AI13.1  the cost of a look: the worker's and the standard bot's, at every level, on every shipped map after 3,000 ticks of a match
//   AI13.2  whole matches of four standard bots (A5): the budget in every window of releases, nothing filtered or rejected, bit-reproducible, replayed without any bot
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

// Whether a seat's releases (their ticks, sorted) keep to the budget of the level in every window: no window holds more commands than the bucket and the rate allow (the bound of AI2.3)
bool within_budget(const std::vector<uint64_t>& ticks, const Profile& p) {
    for (size_t i = 0; i < ticks.size(); ++i) {
        for (size_t j = i; j < ticks.size(); ++j) {
            const uint64_t len = ticks[j] - ticks[i] + 1;
            const uint64_t n = j - i + 1;
            if (n * 1000u > static_cast<uint64_t>(p.burst) * 1000u + (len * p.rate_milli_cps + 19u) / 20u) return false;
        }
    }
    return true;
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

    TEST_CASE("AI13.2 Whole Matches Of Four Standard Bots (Acceptance A5): At Every Level On TREASURE, And On TINY With Several Seeds (Every Style Is Drawn), No Window Of Releases Holds More Commands Than The Bucket And The Rate Allow, Nothing Is Filtered Or Rejected, The Match Is Bit-Reproducible And Its Commands Alone Replay To The Same Hashes Without Any Bot")
    {
        struct Case {
            const char* map;
            Level level;
            uint32_t seed;
        };
        const Case cases[] = {{"TREASURE", Level::Easy, 1}, {"TREASURE", Level::Medium, 1}, {"TREASURE", Level::Hard, 1}, {"TINY", Level::Hard, 1}, {"TINY", Level::Hard, 2},
                              {"TINY", Level::Hard, 3}, {"TINY", Level::Hard, 4}, {"TINY", Level::Medium, 1}, {"TINY", Level::Medium, 2}, {"TINY", Level::Medium, 3}, {"TINY", Level::Medium, 4}};
        for (const Case& c : cases) {
            assets::LevelData level_data;
            ASSERT_TRUE(level_data.load_from_file(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/" + c.map + ".LVL"));
            ArenaSpec spec;
            spec.level = &level_data;
            spec.seed = c.seed;
            spec.latency_ticks = 0;                                                                        // the tick of a record is then the tick of the release
            spec.record = true;
            for (uint8_t seat = 0; seat < 4; ++seat) {
                BotSpec b;
                b.seat = seat;
                b.kind = "standard";
                b.level = c.level;
                spec.bots.push_back(b);
            }
            const ArenaResult r = play_match(spec);
            ASSERT_TRUE(r.error.empty() && r.match_over);
            const Profile p = profile_for(c.level);
            for (uint8_t seat = 0; seat < 4; ++seat) {
                std::vector<uint64_t> ticks;
                for (const RecordedCommand& rc : r.log) {
                    if (rc.command.issuer == seat) ticks.push_back(rc.tick);
                }
                ASSERT_TRUE(std::is_sorted(ticks.begin(), ticks.end()));
                ASSERT_TRUE(within_budget(ticks, p));
                ASSERT_EQ(ticks.size(), static_cast<size_t>(r.seats[seat].stats.released));
                ASSERT_TRUE(r.seats[seat].stats.rejected == 0 && r.seats[seat].stats.filtered == 0);                  // nothing that a person could not click, nothing refused by the engine
                ASSERT_TRUE(r.seats[seat].spec.kind == "standard" && r.seats[seat].runs == "standard" && !r.seats[seat].style.empty());
            }
            const ArenaResult again = play_match(spec);
            ASSERT_TRUE(again.hash == r.hash && again.checkpoints == r.checkpoints && again.log.size() == r.log.size());          // bit-reproducible, styles and all
            const ReplayResult replay = replay_commands(spec, r);
            ASSERT_TRUE(replay.ok && replay.hash == r.hash);                                               // the commands alone: no bot is needed to replay the match
        }
    } TEST_END();
}
