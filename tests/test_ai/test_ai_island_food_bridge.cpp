// AI41: the bridge for food of the standard bot (island_tasks.hpp, "bridges for food"; docs/BOTS.md "Islands"): when the workers stand idle and the food that the walkers and the ferry serve
// well is low, one Swimmer that is not carrying leaves the ferry and digs a bridge to a pile that the ferry does not serve, two Swimmers stay with the ferry, and the bridge keeps off the piles
// that another hill's island reaches as easily. ISLANDS only (one seat alone, or four seats for the contested piles).
#include "ai_test.hpp"
#include "island_fixture.hpp"

namespace {

using namespace island_test;
using namespace ants;
using namespace ants::ai;

uint32_t swimmers_of(const sim::SimulationEngine& sim, uint8_t seat) {
    uint32_t n = 0;
    for (const auto& a : sim.get_world_state().ants) n += a.player_id == seat && a.raw_type == sim::AntType::Swimmer ? 1u : 0u;
    return n;
}

// One Medium seat alone on ISLANDS with three Swimmers given and the ferry on, one Swimmer to a pile, so that piles are left that it does not serve; the bridge starts at once (no waiting)
void lone_seat(Match& m, bool bridge_food, uint32_t keep, bool share) {
    m.builders = 0;
    m.ferry = true;
    m.init("ISLANDS", 1, 0x01, Level::Medium, 0x01, [=](LevelPlan& p) {
        p.island_bridge_food = bridge_food;
        p.island_bridge_from = 0;
        p.island_bridge_idle = 0;
        p.island_bridge_low = 1000000;
        p.island_ferry_keep = keep;
        p.island_ferry_per_pile = 1;
        p.island_bridge_share = share;
    });
    m.sim.spawn_unit(0, sim::AntType::Swimmer, m.ctl->map().hill(0).starts[0]);
    m.sim.spawn_unit(0, sim::AntType::Swimmer, m.ctl->map().hill(0).starts[0]);
}

}  // namespace

void run_island_food_bridge_tests() {
    TEST_CASE("AI41.1 With The Ferry At One Swimmer To A Pile And Workers Idle, One Of Three Swimmers Is Taken From The Ferry And Digs A Bridge (ISLANDS, One Medium Seat): Chains Are Finished, Two Swimmers Stay Alive For The Ferry, Nothing Drowns")
    {
        Match m;
        lone_seat(m, true, 2, false);
        uint32_t chosen = 0;                                                               // the chains that were begun, and the ferry's Swimmers on the pile of each at that look
        uint32_t served = 0;
        uint32_t last_pile = ~uint32_t{0};
        for (int t = 0; t < 3600; ++t) {
            m.tick();
            const uint32_t pile = m.island(0).chain_pile();
            if (pile != ~uint32_t{0} && pile != last_pile) {
                ++chosen;
                served += m.bot(0)->ferry().load(pile);
            }
            last_pile = pile;
        }
        ASSERT_TRUE(chosen >= 1);
        ASSERT_EQ(served, 0u);                                                             // never a bridge to a pile that the ferry works
        ASSERT_TRUE(m.island(0).bridge_builds() >= 1);
        ASSERT_TRUE(m.island(0).tiles_ordered() >= 2);
        ASSERT_TRUE(m.island(0).chains_finished() >= 1);
        ASSERT_EQ(swimmers_of(m.sim, 0), 3u);
        ASSERT_TRUE(m.bot(0)->ferry().sent() >= 2);
        ASSERT_EQ(m.total_unforced(), 0);
    } TEST_END();

    TEST_CASE("AI41.2 With The Bridge For Food Off (Every Level Plan Before It, And `ibfood=0`) The Same Seat Digs Nothing And Takes No Swimmer From The Ferry")
    {
        Match m;
        lone_seat(m, false, 2, false);
        m.run(3600);
        ASSERT_EQ(m.island(0).bridge_builds(), 0u);
        ASSERT_EQ(m.island(0).tiles_ordered(), 0u);
        ASSERT_EQ(m.island(0).chains_finished(), 0u);
    } TEST_END();

    TEST_CASE("AI41.3 The Ferry Is Not Starved: With Three Swimmers And Three Kept For The Ferry No Swimmer Is Taken And Nothing Is Dug")
    {
        Match m;
        lone_seat(m, true, 3, false);
        m.run(3600);
        ASSERT_EQ(m.island(0).bridge_builds(), 0u);
        ASSERT_EQ(m.island(0).tiles_ordered(), 0u);
    } TEST_END();

    TEST_CASE("AI41.4 The Bridge Waits For The Clock And For Idle Workers: With The Start Tick Set Past The Run, Or With The Low Mark Of Food At Zero While The Ferry Serves Piles, Nothing Is Dug")
    {
        {
            Match m;
            m.builders = 0;
            m.ferry = true;
            m.init("ISLANDS", 1, 0x01, Level::Medium, 0x01, [](LevelPlan& p) {
                p.island_bridge_food = true;
                p.island_bridge_from = 100000;
                p.island_bridge_idle = 0;
                p.island_bridge_low = 1000000;
                p.island_ferry_per_pile = 1;
            });
            m.sim.spawn_unit(0, sim::AntType::Swimmer, m.ctl->map().hill(0).starts[0]);
            m.sim.spawn_unit(0, sim::AntType::Swimmer, m.ctl->map().hill(0).starts[0]);
            m.run(2400);
            ASSERT_EQ(m.island(0).bridge_builds(), 0u);
        }
        {
            Match m;
            m.builders = 0;
            m.ferry = true;
            m.init("ISLANDS", 1, 0x01, Level::Medium, 0x01, [](LevelPlan& p) {
                p.island_bridge_food = true;
                p.island_bridge_from = 0;
                p.island_bridge_idle = 0;
                p.island_bridge_low = 0;                                                   // the ferry serves piles worth more than this: no bridge yet
                p.island_ferry_per_pile = 1;
            });
            m.sim.spawn_unit(0, sim::AntType::Swimmer, m.ctl->map().hill(0).starts[0]);
            m.sim.spawn_unit(0, sim::AntType::Swimmer, m.ctl->map().hill(0).starts[0]);
            m.run(600);
            ASSERT_EQ(m.island(0).bridge_builds(), 0u);
        }
    } TEST_END();

    TEST_CASE("AI41.5 No Bridge To A Pile That Another Hill's Island Reaches As Easily: Four Hard Seats On ISLANDS Dig No Bridge For Food In 6,000 Ticks, And With `island_bridge_share` The Contested Piles Are Dug For")
    {
        for (const bool share : {false, true}) {
            Match m;
            m.builders = 0;
            m.expedition = true;
            m.ferry = true;
            m.init("ISLANDS", 1, 0x0F, Level::Hard, 0x0F, [=](LevelPlan& p) {
                p.island_bridge_from = 0;
                p.island_bridge_idle = 0;
                p.island_bridge_low = 1000000;
                p.island_ferry_per_pile = 1;
                p.island_bridge_share = share;
            });
            m.run(6000);
            uint32_t digs = 0;
            for (uint8_t s = 0; s < 4; ++s) digs += m.island(s).tiles_ordered();
            if (share) ASSERT_TRUE(digs > 0);
            else ASSERT_EQ(digs, 0u);
        }
    } TEST_END();

    TEST_CASE("AI41.6 The Plans Of The Levels: The Bridge For Food Is On For Medium And Hard, Off For Easy; The Contested Piles Are Left To The Ferry, Two Swimmers Stay With It, A Bridge Is One Strait (Four Tiles)")
    {
        ASSERT_TRUE(!plan_for(Level::Easy).island_bridge_food);
        ASSERT_TRUE(plan_for(Level::Medium).island_bridge_food);
        ASSERT_TRUE(plan_for(Level::Hard).island_bridge_food);
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            const LevelPlan p = plan_for(level);
            ASSERT_TRUE(!p.island_bridge_share);
            ASSERT_TRUE(!p.island_bridge_rest);
            ASSERT_EQ(p.island_ferry_keep, 2u);
            ASSERT_EQ(p.island_bridge_tiles, 4u);
            ASSERT_EQ(p.island_builders, 0u);
        }
    } TEST_END();
}

