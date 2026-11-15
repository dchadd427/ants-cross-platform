// AI44: the flower link (island_link.hpp, PowerUpTask::watch_link, docs/BOTS.md "The flower link"). ISLANDS, one Hard bot on seat 0: a Swimmer digs the one tile between the western half of the ring
// and the top flower strip, an idle ant of the ring walks over and takes the Swimmer that the flower lets fall. The expedition, the miner and the mines are off, so the ants that a test puts on the
// ring are the only ones there.
#include <functional>
#include <memory>

#include "ai_test.hpp"
#include "b41_helpers.hpp"
#include "ants_ai/standard_bot.hpp"
#include "ants_sim/movement_tables.hpp"

namespace {

using namespace ai_test;
using namespace ants;
using namespace ants::ai;

struct Lab {
    sim::SimulationEngine sim;
    std::unique_ptr<RecordingSink> sink;
    std::unique_ptr<BotController> ctl;

    explicit Lab(const std::function<void(LevelPlan&)>& tweak, const char* map = "ISLANDS", uint8_t roster = 0x01) {
        start_match(sim, map, 1, roster);
        sink = std::make_unique<RecordingSink>(sim, true);
        ctl = std::make_unique<BotController>(sim, 1);
        LevelPlan plan = plan_for(Level::Hard);
        plan.island_expedition = false;
        plan.island_fly_miner = false;
        plan.island_mines = false;
        plan.island_builders = 0;
        plan.island_flower_thief = false;                                                       // (a test that takes a Thief says so)
        if (tweak) tweak(plan);
        BotSpec spec;
        spec.seat = 0;
        spec.kind = "standard";
        spec.level = Level::Hard;
        std::string why;
        ctl->add(spec, std::make_unique<StandardBot>(plan), *sink, why);
    }
    void run(int ticks) {
        for (int i = 0; i < ticks; ++i) {
            sim.tick();
            ctl->on_tick(sim);
            sim.clear_news_events();
            sim.clear_audio_events();
        }
    }
    StandardBot* bot() { return dynamic_cast<StandardBot*>(ctl->bot(0)); }
    const sim::AntSnapshot* ant(uint32_t id) const {
        for (const auto& a : sim.get_world_state().ants) {
            if (a.id == id) return &a;
        }
        return nullptr;
    }
    bool bridge_at(TileCoord t) const { return sim.grid().get_cell(t).has_completed_bridge(); }
};

const TileCoord kBridge{23, 7};          // the water tile between the western half of the ring (22, 6) and the top strip (24, 6)
const TileCoord kDrop{30, 5};            // the drop tile of the top flower
const TileCoord kRing{20, 6};            // a tile of the western half of the ring, near the bridge
const TileCoord kStrip{27, 4};           // a tile of the top strip

bool on_strip(const sim::AntSnapshot* a) { return a != nullptr && a->tile_x >= 24 && a->tile_y <= 6; }

}  // namespace

void run_flower_link_tests() {
    TEST_CASE("AI44.1 The Links Are Found In The Analysis: On ISLANDS Each Ring Half Is Joined To The Strip Whose Flower Is One Water Tile Away (West To The Top Strip At (23, 7), East To The Bottom One At (36, 52)), Not A Satellite; On SMALL There Is None")
    {
        Lab lab({});
        lab.run(50);
        const auto& links = lab.bot()->flower_link().link_list();
        ASSERT_EQ(links.size(), 2u);
        bool west = false;
        bool east = false;
        for (const auto& l : links) {
            ASSERT_EQ(l.tiles.size(), 1u);
            west = west || (l.tiles[0] == kBridge && l.drop == kDrop);
            east = east || (l.tiles[0] == TileCoord{36, 52} && l.drop == TileCoord{30, 56});
        }
        ASSERT_TRUE(west && east);
        Lab small({}, "SMALL");
        small.run(50);
        ASSERT_EQ(small.bot()->flower_link().link_list().size(), 0u);
    } TEST_END();

    TEST_CASE("AI44.2 A Swimmer Digs The Tile For An Ant That Waits On The Ring Half, And The Ant Walks Over To The Strip (A Swimmer And A Worker On The Ring): The Tile Stands Within 500 Ticks And The Worker Is On The Strip")
    {
        Lab lab({});
        lab.sim.spawn_unit(0, sim::AntType::Swimmer, TileCoord{19, 6});
        const uint32_t worker = lab.sim.spawn_unit(0, sim::AntType::Worker, kRing);
        lab.run(500);
        ASSERT_TRUE(lab.bridge_at(kBridge));
        ASSERT_TRUE(lab.bot()->flower_link().tiles_ordered() >= 1u);
        ASSERT_TRUE(on_strip(lab.ant(worker)));
        ASSERT_EQ(lab.bot()->flower_link().failures(), 0u);
        lab.run(900);                                                                           // (the first drop lands at tick 1,217: if it is a Swimmer the worker has taken it)
        ASSERT_TRUE(lab.ant(worker)->raw_type == sim::AntType::Worker || lab.ant(worker)->raw_type == sim::AntType::Swimmer);
    } TEST_END();

    TEST_CASE("AI44.3 Nothing Is Dug For Nobody, Or With The Switch Off: A Swimmer Alone On The Ring Digs Nothing (The Hill's Own Workers Are On Their Island: They Wait For Nothing On The Ring); With `island_flower_link` Off The Worker Stays")
    {
        {
            Lab lab({});
            lab.sim.spawn_unit(0, sim::AntType::Swimmer, TileCoord{19, 6});
            lab.run(1200);
            ASSERT_EQ(lab.bot()->flower_link().tiles_ordered(), 0u);
            ASSERT_TRUE(!lab.bridge_at(kBridge));
        }
        {
            Lab lab([](LevelPlan& p) { p.island_flower_link = false; });
            lab.sim.spawn_unit(0, sim::AntType::Swimmer, TileCoord{19, 6});
            const uint32_t worker = lab.sim.spawn_unit(0, sim::AntType::Worker, kRing);
            lab.run(2000);
            ASSERT_EQ(lab.bot()->flower_link().tiles_ordered(), 0u);
            ASSERT_TRUE(!on_strip(lab.ant(worker)));
        }
    } TEST_END();

    TEST_CASE("AI44.4 The Ant At The Strip Takes The Swimmer That Lies On The Drop Tile (A Worker On The Strip, A Swimmer Power-Up Put On The Drop Tile): It Is A Swimmer Within 400 Ticks; With Three Swimmers Already And `island_flower_swimmers` 0 It Takes Nothing, With 2 It Takes It")
    {
        for (const uint32_t extra : {0u, 2u}) {
            Lab lab([extra](LevelPlan& p) { p.island_flower_swimmers = extra; p.island_ferry = false; });
            for (int i = 0; i < 3; ++i) lab.sim.spawn_unit(0, sim::AntType::Swimmer, TileCoord{10 + i, 4});
            const uint32_t worker = lab.sim.spawn_unit(0, sim::AntType::Worker, kStrip);
            lab.sim.grid_mut().place_powerup(kDrop.x, kDrop.y, 5);
            lab.run(400);
            ASSERT_TRUE(lab.ant(worker) != nullptr);
            ASSERT_EQ(lab.ant(worker)->raw_type == sim::AntType::Swimmer, extra == 2u);
        }
    } TEST_END();

    TEST_CASE("AI44.5 A Fire Ant On The Strip Takes The Swimmer As Well, And Leaves Its Hat; A Thief Drop Is Left Alone Unless `island_flower_thief` Is On")
    {
        {
            Lab lab({});
            const uint32_t fire = lab.sim.spawn_unit(0, sim::AntType::Fire, kStrip);
            lab.sim.grid_mut().place_powerup(kDrop.x, kDrop.y, 5);
            lab.run(400);
            ASSERT_TRUE(lab.ant(fire)->raw_type == sim::AntType::Swimmer);
        }
        for (const bool thief : {false, true}) {
            Lab lab([thief](LevelPlan& p) { p.island_flower_thief = thief; }, "ISLANDS", 0x03);
            const uint32_t walker = lab.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{44, 3});                // (an enemy ant that walks: the bot sees a team that plays, and wants Thieves of its own)
            lab.sim.apply_command(b41::command_of(CommandType::GroupMove, 1, {walker}, 52, 4));
            const uint32_t worker = lab.sim.spawn_unit(0, sim::AntType::Worker, kStrip);
            lab.sim.grid_mut().place_powerup(kDrop.x, kDrop.y, 3);
            lab.run(500);
            ASSERT_EQ(lab.ant(worker)->raw_type == sim::AntType::Thief, thief);
        }
    } TEST_END();

    TEST_CASE("AI44.6 A Tile That Is Gone Is Dug Again While An Ant Waits (Three Swimmers And One Worker On The Ring; A Second Worker Comes At Tick 3,500, When The Tile Has Little Life Left): The Tile Is Dug At Once, Goes At 3,600 Ticks, Is Dug Again For The Second Worker, Which Crosses; Nobody Drowns; With `island_flower_swimmers` 0 (The Bot Has The Three It Wants) Nothing Is Dug")
    {
        for (const uint32_t extra : {2u, 0u}) {
            Lab lab([extra](LevelPlan& p) { p.island_flower_swimmers = extra; });
            for (int i = 0; i < 3; ++i) lab.sim.spawn_unit(0, sim::AntType::Swimmer, TileCoord{19, 6 - i});
            lab.sim.spawn_unit(0, sim::AntType::Worker, kRing);
            lab.run(3500);
            const uint32_t late = lab.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{19, 5});
            lab.run(2200);
            if (extra == 0u) {
                ASSERT_EQ(lab.bot()->flower_link().tiles_ordered(), 0u);
                continue;
            }
            ASSERT_EQ(lab.bot()->flower_link().tiles_ordered(), 2u);                             // (the tile went at 3,600 ticks and was dug again for the ant that came)
            ASSERT_EQ(lab.bot()->flower_link().failures(), 0u);
            ASSERT_TRUE(lab.ant(late) != nullptr);
            ASSERT_TRUE(on_strip(lab.ant(late)) || lab.ant(late)->raw_type == sim::AntType::Swimmer);       // (nobody went down with the tile)
        }
    } TEST_END();

    TEST_CASE("AI44.7 An Enemy Ant Beside The Drop Tile Does Not Make The Watcher Give The Drop Up (An Enemy Worker On The Tile Next To The Drop Tile, Equally Near): The Watcher Takes The Swimmer Within 400 Ticks")
    {
        Lab lab({}, "ISLANDS", 0x03);
        const uint32_t worker = lab.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{29, 4});
        lab.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{31, 4});
        lab.sim.grid_mut().place_powerup(kDrop.x, kDrop.y, 5);
        lab.run(400);
        ASSERT_TRUE(lab.ant(worker) != nullptr);
        ASSERT_TRUE(lab.ant(worker)->raw_type == sim::AntType::Swimmer);
    } TEST_END();
}
