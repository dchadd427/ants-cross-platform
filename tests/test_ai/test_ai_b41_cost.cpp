// What a look of the standard bot costs (B4-1, AI13.1, acceptance A6): the time of one look (BotView::build + think) at the three levels on every shipped map, in the middle of a match with
// four bots, next to the worker's. A measurement with a loose bound (the machine of a test run is shared and busy); the numbers are printed and are the ones of docs/BOTS.md ("Cost").
//
//   AI13.1  the cost of a look: the worker's and the standard bot's, at every level, on every shipped map after 3,000 ticks of a match
//   AI13.2  whole matches of four standard bots (A5): the budget in every window of releases, nothing filtered or rejected, bit-reproducible, replayed without any bot
//   AI13.3  unknown worlds: random terrain, lakes, rocks, piles that nobody can reach and power-ups anywhere, four standard bots of random levels: nothing refused, bit-reproducible
//   AI13.4  a raid click that the filter refuses (a power-up on the enemy hill's entrance) is tried once per 900 ticks, not at every look
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

    TEST_CASE("AI13.3 Unknown Worlds (Robustness, For The Community Maps That A Server Hosts): Sixteen Random Worlds (Patches Of Sand, Mud, Dirt And Water, Rocks, Piles That Nobody May Reach, Power-Ups Anywhere, Even Inside A Rock) Played For 2,400 Ticks By Four Standard Bots Of Random Levels And Styles: No Command Filtered Or Rejected, Nothing Crashes, The Bots Act, And A Second Run Ends On The Same State Hash")
    {
        const auto random_world = [&](sim::SimulationEngine& sim, uint32_t seed) {
            build_world(sim, seed, 6);                                                                         // four hills at the corners, six workers in front of each
            BotRng wr(mix64(0xF00Du + seed * 7919u));
            static const TileCoord kHills[4] = {{4, 4}, {50, 4}, {4, 50}, {50, 50}};
            const auto near_a_hill = [&](int32_t x, int32_t y) {
                for (const TileCoord& h : kHills) {
                    if (std::abs(x - h.x) <= 13 && std::abs(y - h.y) <= 13) return true;
                }
                return false;
            };
            const auto spot = [&]() {
                for (int tries = 0; tries < 200; ++tries) {
                    const int32_t x = 3 + static_cast<int32_t>(wr.below(54));
                    const int32_t y = 3 + static_cast<int32_t>(wr.below(54));
                    if (!near_a_hill(x, y)) return TileCoord{x, y};
                }
                return TileCoord{30, 30};
            };
            for (int patch = 0; patch < 24; ++patch) {                                                         // blobs of one terrain class (water among them)
                const TileCoord c = spot();
                const int r = 2 + static_cast<int>(wr.below(5));
                const uint8_t cls = static_cast<uint8_t>(std::array<uint8_t, 6>{1, 3, 4, 3, 2, 2}[wr.below(6)]);
                for (int y = std::max(1, c.y - r); y <= std::min(58, c.y + r); ++y) {
                    for (int x = std::max(0, c.x - r); x <= std::min(59, c.x + r); ++x) {
                        if ((x - c.x) * (x - c.x) + (y - c.y) * (y - c.y) <= r * r && !near_a_hill(x, y)) sim.grid_mut().set_terrain_class(x, y, cls);
                    }
                }
            }
            for (int rock = 0; rock < 60; ++rock) {
                const TileCoord c = spot();
                sim.set_terrain(c.x, c.y, sim::TERRAIN_OBSTACLE);
            }
            for (int pile = 0; pile < 8; ++pile) {
                const TileCoord c = spot();
                add_pile(sim, c.x, c.y, static_cast<uint16_t>(20 + wr.below(30)), static_cast<uint16_t>(10 + wr.below(30)));
            }
            for (int powerup = 0; powerup < 8; ++powerup) {                                                    // anywhere, a rock or a lake included
                const TileCoord c = spot();
                sim.grid_mut().place_powerup(c.x, c.y, static_cast<uint8_t>(1 + wr.below(5)));
            }
        };
        const auto play = [&](uint32_t seed, ai::BotController::SeatStats stats[4]) {
            sim::SimulationEngine sim;
            random_world(sim, seed);
            RecordingSink sink(sim, true);
            BotController controller(sim, seed);
            BotRng pick(mix64(0xBEEFu + seed));
            std::string why;
            for (uint8_t seat = 0; seat < 4; ++seat) {
                BotSpec spec;
                spec.seat = seat;
                spec.kind = "standard";
                spec.level = static_cast<Level>(pick.below(3));
                if (!controller.add(spec, sink, why)) return uint64_t{0};
            }
            for (int t = 0; t < 2400; ++t) {
                sim.tick();
                sim.clear_news_events();
                sim.clear_audio_events();
                controller.on_tick(sim);
            }
            for (uint8_t seat = 0; seat < 4; ++seat) stats[seat] = controller.stats(seat);
            return sim.state_hash().total;
        };
        size_t acting = 0;
        for (uint32_t seed = 1; seed <= 16; ++seed) {
            ai::BotController::SeatStats a[4];
            ai::BotController::SeatStats b[4];
            const uint64_t first = play(seed, a);
            ASSERT_TRUE(first != 0);
            for (uint8_t seat = 0; seat < 4; ++seat) {
                ASSERT_TRUE(a[seat].filtered == 0 && a[seat].rejected == 0);
                acting += a[seat].released > 0 ? 1u : 0u;
            }
            const uint64_t second = play(seed, b);
            ASSERT_EQ(first, second);                                                                          // the same world and seeds: the same match, bit for bit
        }
        ASSERT_TRUE(acting >= 48);                                                                             // (most of the 64 seats gave at least one order)
    } TEST_END();

    TEST_CASE("AI13.4 A Refused Raid Click Is Not Repeated At Every Look: A Power-Up Lies On The Entrance Of The Enemy Hill (A Click Onto It Would Take The Power-Up, So The Filter Refuses It); In Three Minutes A Hard Standard Bot With A Thief Is Refused Once Per 900 Ticks, Not At Every Look (Every 4 Ticks: About 900 Times)")
    {
        sim::SimulationEngine sim;
        empty_field(sim, 77);                                                                              // team 1's hill is at (50, 4): its entrance is (51, 5)
        sim.set_player_score(1, 200);                                                                      // it has points to take
        sim.grid_mut().place_powerup(51, 5, 3);
        sim.spawn_unit(0, sim::AntType::Thief, TileCoord{30, 10});
        RecordingSink sink(sim, true);
        BotController ctl(sim, 5);
        LevelPlan plan = plan_for(Level::Hard);
        plan.gate = false;
        BotSpec spec;
        spec.seat = 0;
        spec.level = Level::Hard;
        std::string why;
        ASSERT_TRUE(ctl.add(spec, std::make_unique<StandardBot>(plan), sink, why));
        for (int t = 0; t < 3600; ++t) {
            sim.tick();
            sim.clear_news_events();
            sim.clear_audio_events();
            ctl.on_tick(sim);
        }
        const BotController::SeatStats& st = ctl.stats(0);
        ASSERT_TRUE(st.filtered >= 3);                                                                     // the raid was tried again after every 900 ticks ...
        ASSERT_TRUE(st.filtered <= 3600 / 900 + 1);                                                        // ... and only then
        ASSERT_EQ(st.rejected, 0u);
        for (const auto& e : sink.log) ASSERT_FALSE(e.second.type == CommandType::GroupSpecial && e.second.tile_x == 51 && e.second.tile_y == 5);      // nothing refused ever left
    } TEST_END();

    TEST_CASE("AI13.5 An Ant That Dies On The Entrance Of The Enemy Hill Is Still Under The Pointer (The Controller Refuses A Click On It): The Thief's Raid Goes By Another Tile Of The Mound, Is Not Refused And The Hill Is Not Left Alone For 900 Ticks")
    {
        sim::SimulationEngine sim;
        empty_field(sim, 77);                                                                              // team 1's hill is at (50, 4): its entrance is (51, 5)
        sim.set_player_score(1, 200);
        const uint32_t dying = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{51, 5});
        sim.get_unit(dying).hp = 1;
        const uint32_t striker = sim.spawn_unit(2, sim::AntType::Worker, TileCoord{52, 5});
        sim.execute_melee_attack(striker, dying);                                                          // (one blow kills it: it dies on its clip, about 42 ticks, which the world lists)
        sim.spawn_unit(0, sim::AntType::Thief, TileCoord{30, 10});
        for (int t = 0; t < 2; ++t) sim.tick();
        size_t listed = 0;
        for (const sim::AntSnapshot& a : sim.get_world_state().ants) listed += a.tile_x == 51 && a.tile_y == 5 && a.hp == 0 ? 1u : 0u;
        ASSERT_EQ(listed, 1u);
        RecordingSink sink(sim, true);
        BotController ctl(sim, 5);
        ctl.set_start_hold(0);
        LevelPlan plan = plan_for(Level::Hard);
        plan.gate = false;
        BotSpec spec;
        spec.seat = 0;
        spec.level = Level::Hard;
        std::string why;
        ASSERT_TRUE(ctl.add(spec, std::make_unique<StandardBot>(plan), sink, why));
        for (int t = 0; t < 40; ++t) {
            sim.tick();
            sim.clear_news_events();
            sim.clear_audio_events();
            ctl.on_tick(sim);
        }
        const BotController::SeatStats& st = ctl.stats(0);
        ASSERT_EQ(st.filtered, 0u);                                                                        // nothing was refused ...
        size_t raids = 0;
        for (const auto& e : sink.log) {
            if (e.second.type != CommandType::GroupSpecial) continue;
            ++raids;
            ASSERT_FALSE(e.second.tile_x == 51 && e.second.tile_y == 5);                                   // ... the click is not on the dying ant's tile ...
        }
        ASSERT_TRUE(raids >= 1);                                                                           // ... and the raid was ordered at once (another tile of the mound)
    } TEST_END();
}
