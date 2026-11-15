// AI48: the reach guard (docs/BOTS.md, "The can't-go loop", orders over water): a standard bot does not order an ant to a place that its own walk cannot reach on the map as it is now.
// The world of the first six is b41_helpers.hpp's field of 60 x 60 with four hills and a band of water across the middle; the engine's own answer to the click (a can't-go reaction) is
// the yardstick: what the guard takes out is refused by the engine, what it keeps is not.
//
//   AI48.1  a worker's move to a pile across the water is taken out (and the engine refuses it); the same move to a pile on its own side stays
//   AI48.2  an attack on an enemy ant on the other side of the water is taken out, one on its own side stays
//   AI48.3  a pile in a ring of rocks (a walk that ends next to it finds no way) is taken out
//   AI48.4  a Swimmer is never taken out; a Fire Ant crosses a wall of fire that stops a worker; a row of power-ups does not cut a place off
//   AI48.5  the engine's goal of a click on water (the first shore tile of its ring scan) decides: a click on the near half of the band is a walk to the near shore, on the far half to the far shore
//   AI48.6  a group order keeps the ants that can go; a timed chain of which one click is refused goes whole; a stop, a hatch and an ant in a mound are left alone
//   AI48.7  the plan: on at every level, rg=0 switches it off
//   AI48.9  a walk home is a walk to the queue row of the hill (an ant on it is ordered in through a ring of fire, one outside is not), a thief's raid goes by the ring round the other hill
//   AI48.8  whole matches on ISLANDS (Hard, three seeds, with the guard and without it, rg=0): at least 75 percent fewer refused orders and can't-go reactions, and the score does not fall
#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ai_test.hpp"
#include "ants_ai/arena.hpp"
#include "ants_ai/reach_guard.hpp"
#include "ants_ai/standard_bot.hpp"
#include "b41_helpers.hpp"
#include "bot_arena_tune.hpp"

namespace {

using namespace ai_test;
using namespace ants;
using namespace ants::ai;
using namespace b41;

constexpr int kBandLo = 28;                  // the water: columns 28 to 31 on every row (hills 0 and 2 stand west of it, 1 and 3 east)
constexpr int kBandHi = 31;

void water_field(sim::SimulationEngine& sim) {
    empty_field(sim, 5);
    for (int y = 0; y < 60; ++y) {
        for (int x = kBandLo; x <= kBandHi; ++x) sim.set_terrain(x, y, sim::TERRAIN_WATER);
    }
}

// What the guard takes out of one order of seat 0
struct Verdict {
    size_t kept{0};
    std::vector<sim::Command> gone;
    Orders orders;
};

Verdict judge(sim::SimulationEngine& sim, const std::function<void(Orders&)>& give) {
    MapInfo map(sim);
    const BotView view = BotView::build(sim, 0, &map);
    Verdict v;
    give(v.orders);
    v.gone = ReachGuard::filter(view, 0, v.orders);
    v.kept = v.orders.intents().size();
    return v;
}

// The engine's answer to the same click: did the ant begin the can't-go state?
bool engine_refuses(const std::function<void(sim::SimulationEngine&)>& build, const Command& c) {
    sim::SimulationEngine sim;
    build(sim);
    CantGoTally tally;
    for (int i = 0; i < 5; ++i) {
        sim.tick();
        tally.scan(sim, sim.poll_news_events());
    }
    sim.apply_command(c);
    tally.command(c, sim.current_tick());
    for (int i = 0; i < 200; ++i) {
        sim.tick();
        tally.scan(sim, sim.poll_news_events());
        sim.clear_audio_events();
    }
    return tally.seat(c.issuer).began != 0;
}

}  // namespace

void run_reach_tests() {
    TEST_CASE("AI48.1 A Worker's Move To A Pile Across The Water Is Taken Out Of The Orders (And The Engine Refuses It); The Same Move To A Pile On Its Own Side Stays") {
        uint32_t ant = 0;
        const auto build = [&](sim::SimulationEngine& sim) {
            water_field(sim);
            add_pile(sim, 40, 20, 4, 10);
            add_pile(sim, 18, 20, 4, 10);
            ant = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{10, 20});
        };
        sim::SimulationEngine sim;
        build(sim);
        const Verdict across = judge(sim, [&](Orders& o) { o.move({ant}, TileCoord{40, 20}); });
        ASSERT_EQ(across.kept, size_t{0});
        ASSERT_EQ(across.gone.size(), size_t{1});
        ASSERT_EQ(across.gone[0].ants.size(), size_t{1});
        ASSERT_TRUE(engine_refuses(build, command_of(CommandType::GroupMove, 0, {ant}, 40, 20)));
        const Verdict own_side = judge(sim, [&](Orders& o) { o.move({ant}, TileCoord{18, 20}); });
        ASSERT_EQ(own_side.kept, size_t{1});
        ASSERT_TRUE(own_side.gone.empty());
        ASSERT_TRUE(!engine_refuses(build, command_of(CommandType::GroupMove, 0, {ant}, 18, 20)));
    } TEST_END();

    TEST_CASE("AI48.2 An Attack On An Enemy Ant On The Other Side Of The Water Is Taken Out, One On The Ant's Own Side Stays") {
        uint32_t ant = 0, far_enemy = 0, near_enemy = 0;
        const auto build = [&](sim::SimulationEngine& sim) {
            water_field(sim);
            ant = sim.spawn_unit(0, sim::AntType::Combat, TileCoord{10, 30});
            far_enemy = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{40, 30});
            near_enemy = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{16, 36});
        };
        sim::SimulationEngine sim;
        build(sim);
        const Verdict far = judge(sim, [&](Orders& o) { o.attack({ant}, TileCoord{40, 30}); });
        ASSERT_EQ(far.kept, size_t{0});
        ASSERT_EQ(far.gone.size(), size_t{1});
        ASSERT_TRUE(engine_refuses(build, command_of(CommandType::GroupAttack, 0, {ant}, 40, 30)));
        const Verdict near = judge(sim, [&](Orders& o) { o.attack({ant}, TileCoord{16, 36}); });
        ASSERT_EQ(near.kept, size_t{1});
        ASSERT_TRUE(near.gone.empty());
        (void)far_enemy;
        (void)near_enemy;
    } TEST_END();

    TEST_CASE("AI48.3 A Pile In A Ring Of Rocks Is Taken Out: A Walk That Ends Next To It Finds No Way") {
        uint32_t ant = 0;
        const auto build = [&](sim::SimulationEngine& sim) {
            empty_field(sim, 6);
            add_pile(sim, 30, 30, 4, 10);
            for (int dy = -3; dy <= 3; ++dy) {
                for (int dx = -3; dx <= 3; ++dx) {
                    if (std::max(std::abs(dx), std::abs(dy)) == 3) sim.set_terrain(30 + dx, 30 + dy, sim::TERRAIN_OBSTACLE);
                }
            }
            ant = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{10, 20});
        };
        sim::SimulationEngine sim;
        build(sim);
        const Verdict walled = judge(sim, [&](Orders& o) { o.move({ant}, TileCoord{30, 30}); });
        ASSERT_EQ(walled.kept, size_t{0});
        ASSERT_TRUE(engine_refuses(build, command_of(CommandType::GroupMove, 0, {ant}, 30, 30)));
        sim::SimulationEngine shut;                                                              // a pile with a rock on every tile round it: no walk can end next to it at all
        empty_field(shut, 6);
        add_pile(shut, 30, 30, 4, 10);
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx != 0 || dy != 0) shut.set_terrain(30 + dx, 30 + dy, sim::TERRAIN_OBSTACLE);
            }
        }
        const uint32_t w2 = shut.spawn_unit(0, sim::AntType::Worker, TileCoord{10, 20});
        const Verdict closed = judge(shut, [&](Orders& o) { o.move({w2}, TileCoord{30, 30}); });
        ASSERT_EQ(closed.kept, size_t{0});
    } TEST_END();

    TEST_CASE("AI48.4 A Swimmer Is Never Taken Out; A Fire Ant Crosses A Wall Of Fire That Stops A Worker; A Row Of Power-Ups Does Not Cut A Place Off") {
        uint32_t worker = 0, swimmer = 0, fire = 0;
        sim::SimulationEngine sim;
        water_field(sim);
        worker = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{10, 20});
        swimmer = sim.spawn_unit(0, sim::AntType::Swimmer, TileCoord{10, 22});
        fire = sim.spawn_unit(0, sim::AntType::Fire, TileCoord{10, 24});
        const Verdict water = judge(sim, [&](Orders& o) { o.move({worker, swimmer, fire}, TileCoord{40, 20}); });
        ASSERT_EQ(water.kept, size_t{1});                                                        // the group order stays for the Swimmer alone
        ASSERT_EQ(water.orders.intents()[0].command.ants, std::vector<uint32_t>{swimmer});
        ASSERT_EQ(water.gone.size(), size_t{1});
        ASSERT_EQ(water.gone[0].ants.size(), size_t{2});
        sim::SimulationEngine fire_world;
        empty_field(fire_world, 7);
        for (int y = 0; y < 60; ++y) fire_world.set_fire_at(TileCoord{20, y}, 3600);
        const uint32_t w2 = fire_world.spawn_unit(0, sim::AntType::Worker, TileCoord{10, 30});
        const uint32_t f2 = fire_world.spawn_unit(0, sim::AntType::Fire, TileCoord{10, 32});
        const Verdict wall = judge(fire_world, [&](Orders& o) {
            o.move({w2}, TileCoord{30, 30});
            o.move({f2}, TileCoord{30, 32});
        });
        ASSERT_EQ(wall.kept, size_t{1});
        ASSERT_EQ(wall.orders.intents()[0].command.ants, std::vector<uint32_t>{f2});
        sim::SimulationEngine row_world;
        empty_field(row_world, 8);
        for (int y = 0; y < 60; ++y) {
            if (y != 30) row_world.set_terrain(20, y, sim::TERRAIN_OBSTACLE);
        }
        row_world.grid_mut().place_powerup(20, 30, static_cast<uint8_t>(sim::AntType::Fire));
        const uint32_t w3 = row_world.spawn_unit(0, sim::AntType::Worker, TileCoord{10, 30});
        const Verdict row = judge(row_world, [&](Orders& o) { o.move({w3}, TileCoord{30, 30}); });
        ASSERT_EQ(row.kept, size_t{1});
        sim::SimulationEngine bomb_world;                                                        // a line of the seat's own bombs (the tasks that dig defuse or cross their own): no place is cut off by it
        empty_field(bomb_world, 10);
        for (int y = 0; y < 60; ++y) bomb_world.grid_mut().place_bomb(20, static_cast<uint32_t>(y), 0);
        const uint32_t w4 = bomb_world.spawn_unit(0, sim::AntType::Worker, TileCoord{10, 30});
        const Verdict bombs = judge(bomb_world, [&](Orders& o) { o.move({w4}, TileCoord{30, 30}); });
        ASSERT_EQ(bombs.kept, size_t{1});
    } TEST_END();

    TEST_CASE("AI48.5 The Goal Of A Click On Water Is The First Shore Tile Of The Engine's Ring Scan: A Click On The Near Half Of The Band Is A Walk To The Near Shore, On The Far Edge To The Far Shore") {
        uint32_t ant = 0;
        sim::SimulationEngine sim;
        water_field(sim);
        ant = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{10, 20});
        const Verdict near = judge(sim, [&](Orders& o) { o.move({ant}, TileCoord{29, 20}); });
        ASSERT_EQ(near.kept, size_t{1});
        const Verdict far = judge(sim, [&](Orders& o) { o.move({ant}, TileCoord{kBandHi, 20}); });
        ASSERT_EQ(far.kept, size_t{0});
        const auto build = [&](sim::SimulationEngine& s) {
            water_field(s);
            ant = s.spawn_unit(0, sim::AntType::Worker, TileCoord{10, 20});
        };
        ASSERT_TRUE(!engine_refuses(build, command_of(CommandType::GroupMove, 0, {ant}, 29, 20)));
        ASSERT_TRUE(engine_refuses(build, command_of(CommandType::GroupMove, 0, {ant}, kBandHi, 20)));
    } TEST_END();

    TEST_CASE("AI48.6 A Group Order Keeps The Ants That Can Go; A Timed Chain Of Which One Click Is Refused Goes Whole; A Stop, A Hatch And An Ant In A Mound Are Left Alone") {
        sim::SimulationEngine sim;
        water_field(sim);
        const uint32_t a = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{10, 20});
        const uint32_t b = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{44, 20});               // (an ant of seat 0 on the far side)
        const Verdict group = judge(sim, [&](Orders& o) { o.move({a, b}, TileCoord{46, 24}); });
        ASSERT_EQ(group.kept, size_t{1});
        ASSERT_EQ(group.orders.intents()[0].command.ants, std::vector<uint32_t>{b});
        ASSERT_EQ(group.gone.size(), size_t{1});
        ASSERT_EQ(group.gone[0].ants, std::vector<uint32_t>{a});
        const Verdict chain = judge(sim, [&](Orders& o) {
            o.chain(a, {ChainStep{TileCoord{12, 20}, false, 0, 0}, ChainStep{TileCoord{40, 20}, false, 10, 11}});
            o.move({a}, TileCoord{14, 20});
        });
        ASSERT_EQ(chain.kept, size_t{1});                                                          // the chain is gone with both its clicks; the plain move stays
        ASSERT_TRUE(chain.orders.intents()[0].chain == 0u);
        ASSERT_EQ(chain.gone.size(), size_t{2});
        const Verdict other = judge(sim, [&](Orders& o) {
            o.stop({a});
            o.hatch();
        });
        ASSERT_EQ(other.kept, size_t{2});
        ASSERT_TRUE(other.gone.empty());
        MapInfo map(sim);
        const HillInfo& hill = map.hill(0);
        const uint32_t inside = sim.spawn_unit(0, sim::AntType::Worker, hill.entrance);
        const Verdict home = judge(sim, [&](Orders& o) { o.move({inside}, TileCoord{18, 18}); });
        ASSERT_EQ(home.kept, size_t{1});                                                           // (an ant in the mound: the engine decides)
        const Verdict walk_home = judge(sim, [&](Orders& o) { o.move({a}, hill.entrance); });
        ASSERT_EQ(walk_home.kept, size_t{1});                                                      // (a walk home)
    } TEST_END();

    TEST_CASE("AI48.9 A Walk Home Is A Walk To The Queue Row Of The Hill: An Ant On It Is Ordered In Through A Ring Of Fire Walls (The Engine Takes It), An Ant Outside The Ring Is Not (The Engine Refuses It); A Thief's Raid On Another Hill Goes By The Ring Round It") {
        uint32_t inside = 0, outside = 0, thief = 0;
        TileCoord entrance{};
        const auto build = [&](sim::SimulationEngine& sim) {
            empty_field(sim, 9);
            const MapInfo m(sim);
            const HillInfo& h = m.hill(0);
            entrance = h.entrance;
            for (const TileCoord& t : SabotageTask::ring_of(h)) sim.set_fire_at(t, 3600);
            inside = sim.spawn_unit(0, sim::AntType::Worker, h.queue);
            outside = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{20, 20});
            thief = sim.spawn_unit(0, sim::AntType::Thief, TileCoord{30, 12});
        };
        sim::SimulationEngine sim;
        build(sim);
        const Verdict home = judge(sim, [&](Orders& o) {
            o.move({inside}, entrance);
            o.move({outside}, entrance);
        });
        ASSERT_EQ(home.kept, size_t{1});
        ASSERT_EQ(home.orders.intents()[0].command.ants, std::vector<uint32_t>{inside});
        ASSERT_EQ(home.gone.size(), size_t{1});
        ASSERT_EQ(home.gone[0].ants, std::vector<uint32_t>{outside});
        ASSERT_TRUE(engine_refuses(build, command_of(CommandType::GroupMove, 0, {outside}, entrance.x, entrance.y)));
        const MapInfo m(sim);
        const Verdict raid = judge(sim, [&](Orders& o) { o.special(thief, m.hill(1).raid); });
        ASSERT_EQ(raid.kept, size_t{1});
    } TEST_END();

    TEST_CASE("AI48.7 The Plan: The Guard Is On At Every Level And rg=0 Switches It Off") {
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) ASSERT_TRUE(plan_for(level).reach_guard);
        LevelPlan p = plan_for(Level::Hard);
        std::string err;
        ASSERT_TRUE(apply_tune(p, "rg", 0, err) && !p.reach_guard);
        ASSERT_TRUE(apply_tune(p, "rg", 1, err) && p.reach_guard);
        ASSERT_TRUE(!apply_tune(p, "rgx", 1, err));
    } TEST_END();

    TEST_CASE("AI48.8 Whole Matches On ISLANDS: Four Hard Standard Bots, Seeds 1 To 3, With The Guard And Without It (rg=0, The Bot As It Was): At Least 75 Percent Fewer Refused Orders And Can't-Go Reactions, And The Score Does Not Fall") {
        uint64_t orders[2] = {0, 0}, refused[2] = {0, 0}, reactions[2] = {0, 0};
        int64_t score[2] = {0, 0};
        for (const bool guard : {true, false}) {
            for (uint32_t seed = 1; seed <= 3; ++seed) {
                ArenaSpec spec;
                spec.level = &level_of("ISLANDS");
                spec.seed = seed;
                for (uint8_t seat = 0; seat < 4; ++seat) {
                    BotSpec b;
                    b.seat = seat;
                    b.kind = "standard";
                    b.level = Level::Hard;
                    spec.bots.push_back(b);
                }
                spec.factory = [guard](const BotSpec& b) { return std::make_unique<StandardBot>(b.level, b.style, [guard](LevelPlan& p) { p.reach_guard = guard; }); };
                const ArenaResult r = play_match(spec);
                ASSERT_TRUE(r.error.empty());
                ASSERT_TRUE(r.match_over);
                for (const ArenaSeatResult& seat : r.seats) {
                    orders[guard ? 0 : 1] += seat.orders;
                    refused[guard ? 0 : 1] += seat.refused_orders;
                    reactions[guard ? 0 : 1] += seat.cantgo;
                    score[guard ? 0 : 1] += seat.score;
                }
            }
        }
        ASSERT_TRUE(orders[0] >= 2000 && orders[1] >= 2000);                              // the matches were played
        ASSERT_TRUE(refused[1] >= 500 && reactions[1] >= 500);                            // the bot as it was has plenty to lose (about 600 refused and 1,000 reactions a seat and match... see the docs)
        ASSERT_TRUE(refused[0] * 4u <= refused[1]);
        ASSERT_TRUE(reactions[0] * 4u <= reactions[1]);
        ASSERT_TRUE(score[0] * 100 >= score[1] * 97);                                     // (the same score within 3 percent: the guard drops only orders that were refused)
    } TEST_END();
}
