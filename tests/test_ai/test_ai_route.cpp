// AI28: bomb routing for a Hard bot (docs/reverse_engineering/bomb_gates.md, "Bomb routing"): a Bomber defuses the bombs on the way of the economy and only those, steps only on tiles
// without a bomb, and the pile stays closed to the workers until the way is clear. Off in every plan (the gate goals' flag); Hard only.
//
//   AI28.1   a band of neutral bombs across the way to the food: the flag on defuses a path through it, loses no ant and banks; every bomb off the path is still there
//   AI28.2   Bombz Away itself: a bomb on every tile and every start ant on one; after the opening no Bomber steps on a bomb, and another team's corner is never touched
//   AI28.3   the flag off, the routing switched off, a Medium bot and a map without bombs: no Bomber held, no pile closed, no order
//   AI28.4   a Bomber that cannot get at the bombs gives up after 90 s and the pile is open again, the Bomber given back
//   AI28.5   Bombz Away, each team alone: the way in is cleared (the tunnel of 13, 8, 12 and 9 bombs at least, the base leg and the pile's neighbours too), the Bombers are given back, and few bombs are gone
#include <map>
#include <string>

#include "ai_test.hpp"
#include "b41_helpers.hpp"
#include "ants_ai/route_task.hpp"

using namespace ai_test;
using namespace ants;
using namespace ants::ai;
using namespace b41;

namespace {

constexpr uint8_t kNeutral = 255;

assets::LevelData load_level(const std::string& path) {
    assets::LevelData level;
    level.load_from_file(path);
    return level;
}

struct BombField {
    sim::SimulationEngine sim;
    int placed{0};
    explicit BombField(uint32_t seed = 3) {
        empty_field(sim, seed, 14400);
        place_crackers(sim, 12, 40, 25);
        for (int k = 0; k < 8; ++k) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{6 + k, 10});
        sim.spawn_unit(0, sim::AntType::Bomber, TileCoord{10, 12});
        for (int y = 20; y < 23; ++y) {
            for (int x = 0; x < 41; ++x) {                                                  // not the whole width: the food is reachable round the band, so it is no gate
                sim.grid_mut().place_bomb(static_cast<uint32_t>(x), static_cast<uint32_t>(y), kNeutral);
                ++placed;
            }
        }
    }
    int bombs() const {
        int n = 0;
        for (int y = 0; y < 60; ++y) {
            for (int x = 0; x < 60; ++x) n += sim.grid().has_bomb_at(TileCoord{x, y}) ? 1 : 0;
        }
        return n;
    }
};

LevelPlan route_plan(bool on = true) {
    LevelPlan plan = peace_plan(Level::Hard);
    plan.gate_goals = on;
    return plan;
}

}  // namespace

void run_route_tests() {
    TEST_CASE("AI28.1 A Band Of Neutral Bombs Across The Way: The Flag On Defuses A Path Through It, Loses No Ant And Banks; The Bombs Off The Path Are Still There") {
        BombField f;
        Rig rig(f.sim, 0, Level::Hard, std::make_unique<StandardBot>(route_plan()));
        auto& bot = rig.as<StandardBot>();
        rig.run(6000);
        ASSERT_TRUE(bot.route().defuses() >= 3u);
        ASSERT_TRUE(bot.route().closed_piles().empty());
        ASSERT_EQ(bot.route().bombers(), 0u);                                            // the way is clear: the Bomber is the economy's again
        ASSERT_TRUE(f.sim.get_player_score(0) > 0);
        ASSERT_TRUE(f.bombs() > f.placed / 2);                                           // a path, not the band
        ASSERT_EQ(f.bombs(), f.placed - static_cast<int>(bot.route().defuses()));              // no bomb went off under an ant
        const BotView end = BotView::build(f.sim, 0, nullptr);
        for (const AntView& a : end.mine()) ASSERT_TRUE(a.hp >= 8);
    } TEST_END();
    TEST_CASE("AI28.2 Bombz Away: A Bomb On Every Tile, Every Start Ant On One; The Opening Costs The Boxed-In Bombers Their Hit Points, After It No Bomber Steps On A Bomb, Each Defuses From A Tile Next To It, And The Far Corner Is Never Touched") {
        sim::SimulationEngine sim;
        sim.init(load_level(std::string(ANTS_SOURCE_DIR) + "/Community-Maps/Bombz Away.lvl"), 7, 0x0F);
        Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(route_plan()));
        auto& bot = rig.as<StandardBot>();
        auto corner = [&] {
            int n = 0;
            for (int y = 19; y < 29; ++y) {
                for (int x = 0; x < 9; ++x) n += sim.grid().has_bomb_at(TileCoord{x, y}) ? 1 : 0;
            }
            return n;
        };
        ASSERT_TRUE(corner() > 60);
        rig.run(200);                                                                     // the opening: the healthiest boxed-in Bomber sets its own bomb off and is thrown (its chain may reach far)
        const int corner_before = corner();
        std::map<uint32_t, int> hp;
        {
            const BotView v = BotView::build(sim, 0, nullptr);
            for (const AntView& a : v.mine()) {
                if (a.type == sim::AntType::Bomber) hp[a.id] = a.hp;
            }
        }
        ASSERT_TRUE(!hp.empty());
        rig.run(3000);
        ASSERT_TRUE(bot.route().defuses() >= 20u);
        ASSERT_FALSE(bot.route().gave_up());
        ASSERT_EQ(corner(), corner_before);                                                // another team's area
        const BotView v = BotView::build(sim, 0, nullptr);
        for (const AntView& a : v.mine()) {
            if (hp.count(a.id) != 0) ASSERT_TRUE(static_cast<int>(a.hp) >= hp[a.id]);       // nobody stepped on a bomb after the opening (the hill heals)
        }
    } TEST_END();
    TEST_CASE("AI28.3 The Flag Off, The Routing Off, A Medium Bot And A Map Without Bombs: No Bomber Held, No Pile Closed, No Order") {
        for (int v = 0; v < 4; ++v) {
            BombField f;
            if (v == 2) {
                for (int y = 20; y < 23; ++y) {
                    for (int x = 0; x < 60; ++x) f.sim.grid_mut().clear_bomb(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
                }
            }
            LevelPlan plan = v == 1 ? peace_plan(Level::Medium) : route_plan(v == 2);
            plan.gate_goals = v != 0;
            plan.gate_route = v != 3;                                                      // (the routing has its own switch below the gate goals')
            Rig rig(f.sim, 0, v == 1 ? Level::Medium : Level::Hard, std::make_unique<StandardBot>(plan));
            auto& bot = rig.as<StandardBot>();
            rig.run(1500);
            ASSERT_EQ(bot.route().defuses(), 0u);
            ASSERT_EQ(bot.route().steps(), 0u);
            ASSERT_EQ(bot.route().bombers(), 0u);
            ASSERT_TRUE(bot.route().closed_piles().empty());
        }
    } TEST_END();
    TEST_CASE("AI28.4 A Bomber That Cannot Get At The Bombs Gives Up After 90 Seconds And The Pile Is Open Again") {
        BombField f;
        for (int dy = -1; dy <= 1; ++dy) {                                              // the Bomber is boxed in by rocks
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx != 0 || dy != 0) f.sim.set_terrain(10 + dx, 12 + dy, sim::TERRAIN_OBSTACLE);
            }
        }
        Rig rig(f.sim, 0, Level::Hard, std::make_unique<StandardBot>(route_plan()));
        auto& bot = rig.as<StandardBot>();
        rig.run(4000);
        ASSERT_TRUE(bot.route().gave_up());
        ASSERT_EQ(bot.route().defuses(), 0u);
        ASSERT_TRUE(bot.route().closed_piles().empty());
        ASSERT_EQ(bot.route().bombers(), 0u);                                           // the ledger is free again
    } TEST_END();
    TEST_CASE("AI28.5 Bombz Away, Each Team Alone: The Way Is Cleared (At Least The Tunnel Of 13, 8, 12 And 9 Bombs), The Bombers Are Given Back, And Few Bombs Are Gone") {
        const int32_t tunnel[4] = {13, 8, 12, 9};
        for (uint8_t seat = 0; seat < 4; ++seat) {
            sim::SimulationEngine sim;
            sim.init(load_level(std::string(ANTS_SOURCE_DIR) + "/Community-Maps/Bombz Away.lvl"), 7, 0x0F);
            auto bombs = [&] {
                int n = 0;
                for (int y = 0; y < 32; ++y) {
                    for (int x = 0; x < 32; ++x) n += sim.grid().has_bomb_at(TileCoord{x, y}) ? 1 : 0;
                }
                return n;
            };
            const int before = bombs();
            Rig rig(sim, seat, Level::Hard, std::make_unique<StandardBot>(route_plan()));
            auto& bot = rig.as<StandardBot>();
            rig.run(9000);
            ASSERT_EQ(bot.route().route_bombs(), 0u);
            ASSERT_FALSE(bot.route().gave_up());
            ASSERT_EQ(bot.route().bombers(), 0u);
            ASSERT_TRUE(bot.route().closed_piles().empty());
            ASSERT_TRUE(static_cast<int32_t>(bot.route().defuses()) >= tunnel[seat]);
            ASSERT_TRUE(before - bombs() >= tunnel[seat]);
            const MapInfo map(sim);
            for (const sim::TileCoord& start : map.hill(seat).starts) ASSERT_FALSE(sim.grid().has_bomb_at(start));      // the base leg: the walk ends on the hill's start tiles
            ASSERT_TRUE(before - bombs() < 200);                                         // a way and its edges, not the field
        }
    } TEST_END();
}
