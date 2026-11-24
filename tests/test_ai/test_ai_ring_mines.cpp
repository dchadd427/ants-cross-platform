// AI43: the mines at the ring pile (miner_flight_task.hpp, MineTask::collect_island_targets, docs/BOT_ISLANDS.md "Islands") and the ferry that sends a stopped Swimmer again (FerryTask::reorder_hurt).
// ISLANDS, one bot on seat 0; a second team is there only where a test needs an enemy ant.
#include <functional>
#include <memory>

#include "ai_test.hpp"
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

    Lab(const std::function<void(LevelPlan&)>& tweak, uint8_t roster = 0x01) {
        start_match(sim, "ISLANDS", 1, roster);
        sink = std::make_unique<RecordingSink>(sim, true);
        ctl = std::make_unique<BotController>(sim, 1);
        LevelPlan plan = plan_for(Level::Hard);
        plan.island_expedition = false;
        plan.island_ferry = false;
        plan.island_builders = 0;
        plan.island_mine_from = 0;
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
    // The own bombs on the map
    std::vector<TileCoord> bombs() const {
        std::vector<TileCoord> out;
        const sim::Grid& grid = sim.grid();
        for (int y = 0; y < static_cast<int>(grid.height()); ++y) {
            for (int x = 0; x < static_cast<int>(grid.width()); ++x) {
                const TileCoord t{x, y};
                if (grid.has_bomb_at(t) && grid.get_cell(t).interactive_owner == 0) out.push_back(t);
            }
        }
        return out;
    }
};

const TileCoord kSatellite{12, 24};     // a satellite west of hill 0 with a Bomber of its own at the start; the western half of the ring (and its pile at (2, 31)) lies beyond a strait of four tiles

}  // namespace

void run_ring_mines_tests() {
    TEST_CASE("AI43.1 A Free Bomber On The Satellite Is Flown To The Island Of The Ring Pile On Bombs Of Its Own (ISLANDS, Seat 0 Alone): It Lands West Of The Strait Within 1,500 Ticks, And Is The Seat's Own Bomber Still")
    {
        Lab lab([](LevelPlan& p) { p.island_mines = false; });
        const uint32_t bomber = lab.sim.spawn_unit(0, sim::AntType::Bomber, kSatellite);
        lab.run(1500);
        const sim::AntSnapshot* a = lab.ant(bomber);
        ASSERT_TRUE(a != nullptr);
        ASSERT_TRUE(a->raw_type == sim::AntType::Bomber);
        ASSERT_TRUE(a->tile_x <= 8);                                                       // on the ring (the satellite is at x 10 .. 15)
        ASSERT_TRUE(lab.bot()->miner_flight().landed() >= 1);
        ASSERT_TRUE(lab.bot()->miner_flight().hops() >= 1);
        ASSERT_TRUE(!lab.bot()->miner_flight().flying());
    } TEST_END();

    TEST_CASE("AI43.2 The Bomber On The Ring Mines The Pile: At Least Four Mines Stand Within Three Tiles Of The Pile After 3,000 Ticks, None Next To It (Two Tiles Away At Least), None Within Six Of The Own Hill, And Every Two Are Two Tiles Apart")
    {
        Lab lab({});
        lab.sim.spawn_unit(0, sim::AntType::Bomber, kSatellite);
        lab.run(3000);
        const std::vector<TileCoord> bombs = lab.bombs();
        const TileCoord pile{2, 31};
        uint32_t near = 0;
        for (const TileCoord& b : bombs) {
            if (b.chebyshev_dist(pile) <= 5) ++near;
            ASSERT_TRUE(b.chebyshev_dist(TileCoord{23, 22}) > 6);
        }
        ASSERT_TRUE(near >= 4);
        for (size_t i = 0; i < bombs.size(); ++i) {
            for (size_t j = i + 1; j < bombs.size(); ++j) ASSERT_TRUE(bombs[i].chebyshev_dist(bombs[j]) >= 2);
        }
        ASSERT_TRUE(lab.bot()->mines().island_planted() >= 4);
    } TEST_END();

    TEST_CASE("AI43.3 With The Switches Off Nothing Flies And Nothing Is Mined: `island_fly_miner` Off Leaves The Bomber On The Satellite; `island_mines` Off Leaves No Mine On The Ring")
    {
        {
            Lab lab([](LevelPlan& p) { p.island_fly_miner = false; });
            const uint32_t bomber = lab.sim.spawn_unit(0, sim::AntType::Bomber, kSatellite);
            lab.run(1500);
            ASSERT_EQ(lab.bot()->miner_flight().started(), 0u);
            ASSERT_TRUE(lab.ant(bomber)->tile_x >= 8);
        }
        {
            Lab lab([](LevelPlan& p) { p.island_mines = false; });
            lab.sim.spawn_unit(0, sim::AntType::Bomber, kSatellite);
            lab.run(2500);
            ASSERT_EQ(lab.bot()->mines().island_planted(), 0u);
        }
    } TEST_END();

    TEST_CASE("AI43.4 The Flight Waits For The Clock: With `island_mine_from` Past The Run The Bomber Stays")
    {
        Lab lab([](LevelPlan& p) { p.island_mine_from = 100000; });
        const uint32_t bomber = lab.sim.spawn_unit(0, sim::AntType::Bomber, kSatellite);
        lab.run(1200);
        ASSERT_EQ(lab.bot()->miner_flight().started(), 0u);
        ASSERT_TRUE(lab.ant(bomber)->tile_x >= 8);
    } TEST_END();

    TEST_CASE("AI43.5 A Swimmer That A Blow Stopped Is Sent Again: An Enemy Combat Ant Beside The Swimmer Right After Its Order Hits It; With `island_ferry_reorder` The Ferry Orders It Again (ended() Counts It), Without It The Swimmer Is Not Ordered Again For A Long Time")
    {
        for (const bool reorder : {true, false}) {
            Lab lab([reorder](LevelPlan& p) { p.island_ferry = true; p.island_ferry_reorder = reorder; p.island_fly_miner = false; p.island_mines = false; }, 0x03);
            const MapInfo probe(lab.sim);
            const TileCoord start = probe.hill(0).starts[0];
            for (int dy = -2; dy <= 2; ++dy) {                                                   // enemy mines on every free ground tile round the gate: the Swimmer steps on one on its way out
                for (int dx = -2; dx <= 2; ++dx) {
                    const TileCoord t{start.x + dx, start.y + dy};
                    if ((dx != 0 || dy != 0) && lab.sim.grid().in_bounds(t) && lab.sim.grid().get_cell(t).can_place_bomb()) lab.sim.grid_mut().place_bomb(static_cast<uint32_t>(t.x), static_cast<uint32_t>(t.y), 1);
                }
            }
            const uint32_t swimmer = lab.sim.spawn_unit(0, sim::AntType::Swimmer, start);
            lab.run(400);
            const uint16_t hp0 = 10;
            const sim::AntSnapshot* after = lab.ant(swimmer);
            ASSERT_TRUE(after != nullptr);
            ASSERT_TRUE(after->hp < hp0);                                                   // (the blow landed)
            if (reorder) ASSERT_TRUE(lab.bot()->ferry().ended() >= 1);
            else ASSERT_EQ(lab.bot()->ferry().ended(), 0u);
        }
    } TEST_END();

    TEST_CASE("AI43.6 The Plans Of The Levels: The Flight, The Mines, The Re-order And Three Swimmers To A Pile Are Hard's Alone; Easy And Medium Are As Before")
    {
        for (const Level level : {Level::Easy, Level::Medium}) {
            const LevelPlan p = plan_for(level);
            ASSERT_TRUE(!p.island_fly_miner && !p.island_mines && !p.island_ferry_reorder);
            ASSERT_EQ(p.island_ferry_per_pile, 2u);
        }
        const LevelPlan h = plan_for(Level::Hard);
        ASSERT_TRUE(h.island_fly_miner && h.island_mines && h.island_ferry_reorder);
        ASSERT_EQ(h.island_ferry_per_pile, 3u);
        ASSERT_EQ(h.island_miners, 1u);
        ASSERT_EQ(h.island_mine_count, 10u);
        ASSERT_EQ(h.island_mine_apart, 2u);
        ASSERT_EQ(h.island_mine_from, 600u);                                                // (the flight starts when the expedition is over)
        ASSERT_TRUE(h.island_mine_reach && !h.island_walker_bridge);
        for (const Level level : {Level::Easy, Level::Medium}) ASSERT_TRUE(!plan_for(level).island_mine_reach && !plan_for(level).island_walker_bridge);
    } TEST_END();

    // AI49: a Bomber that cannot walk to a mine is not sent to lay it, so it stays free for the flight to the ring pile
    TEST_CASE("AI49.1 With A Second Team On The Map, The Bomber On The Satellite Is Not Sent To Lay A Mine On Another Island's Ground: It Is Free For The Flight, Which Starts And Lands Within 2,000 Ticks; With `island_mine_reach` Off (And The Reach Guard Off, Which Would Drop The Refused Orders Itself) The Mine Task Keeps It And No Flight Starts")
    {
        for (const bool reach : {true, false}) {
            Lab lab([&](LevelPlan& p) { p.island_mine_reach = reach; p.reach_guard = false; p.island_mines = false; p.war_free_only = false; p.mine_gate = 3; p.mine_home = 4; p.mine_home_after = 0; p.island_mine_from = 300; }, 0x0f);
            lab.sim.spawn_unit(0, sim::AntType::Bomber, kSatellite);
            lab.run(2000);
            if (reach) {
                ASSERT_TRUE(lab.bot()->miner_flight().started() >= 1);
                ASSERT_TRUE(lab.bot()->miner_flight().landed() >= 1);
            } else {
                ASSERT_EQ(lab.bot()->miner_flight().started(), 0u);
            }
        }
    } TEST_END();

    // A power-up (or food, a lunchbox) on the landing tile is a solid object: the engine does not throw the ant onto it but turns the throw to the next free direction, and here that is water
    TEST_CASE("AI43.7 A Flight Is Flown Only Onto A Landing That The Engine Accepts: A Power-Up On Every Landing Before The Choice Means No Flight, One On The Landing Of The Planted Bomb Never Throws The Bomber Into The Water")
    {
        const auto drowned_within = [](Lab& lab, uint32_t bomber, int ticks) {
            for (int i = 0; i < ticks; i += 5) {
                lab.run(5);
                const sim::AntSnapshot* a = lab.ant(bomber);
                if (a == nullptr || a->state == sim::UnitState::Drowning) return true;
            }
            return false;
        };
        {                                                                                  // before the choice: no landing of the satellite's flights is free, so none is flown
            Lab lab([](LevelPlan& p) { p.island_mines = false; });
            const uint32_t bomber = lab.sim.spawn_unit(0, sim::AntType::Bomber, kSatellite);
            lab.run(1);
            const std::vector<const Flight*> flights = lab.bot()->islands().info().flights_from(lab.ctl->map().component(0, kSatellite));
            ASSERT_TRUE(!flights.empty());
            for (const Flight* f : flights) lab.sim.grid_mut().place_powerup(f->land.x, f->land.y, 3);
            ASSERT_TRUE(!drowned_within(lab, bomber, 1500));
            ASSERT_EQ(lab.bot()->miner_flight().hops(), 0u);
            ASSERT_EQ(lab.bot()->miner_flight().planted(), 0u);                           // (no bomb is laid for a flight that cannot be flown)
        }
        {                                                                                  // after the bomb is planted: the flight is chosen again or given up, the ant stays dry
            Lab lab([](LevelPlan& p) { p.island_mines = false; });
            const uint32_t bomber = lab.sim.spawn_unit(0, sim::AntType::Bomber, kSatellite);
            lab.run(1);
            const std::vector<const Flight*> flights = lab.bot()->islands().info().flights_from(lab.ctl->map().component(0, kSatellite));
            ASSERT_TRUE(!flights.empty());
            int guard = 0;
            while (lab.bombs().empty() && guard++ < 400) lab.run(5);
            ASSERT_TRUE(!lab.bombs().empty());
            const std::vector<TileCoord> planted = lab.bombs();
            for (const Flight* f : flights) {
                if (std::find(planted.begin(), planted.end(), f->bomb) != planted.end()) lab.sim.grid_mut().place_powerup(f->land.x, f->land.y, 3);
            }
            ASSERT_TRUE(!drowned_within(lab, bomber, 1500));
        }
    } TEST_END();

    TEST_CASE("AI49.2 The Reports Name The Flight: first_start And first_landing Are Set Once The Flight Has Landed And Are 0 Before")
    {
        Lab lab([](LevelPlan& p) { p.island_mines = false; });
        lab.sim.spawn_unit(0, sim::AntType::Bomber, kSatellite);
        ASSERT_EQ(lab.bot()->miner_flight().first_start(), 0u);
        lab.run(1500);
        ASSERT_TRUE(lab.bot()->miner_flight().first_start() > 0);
        ASSERT_TRUE(lab.bot()->miner_flight().first_landing() > lab.bot()->miner_flight().first_start());
    } TEST_END();
}
