// AI18: the ferry of the standard bot (island_ferry.hpp, docs/BOTS.md "Islands"): a Swimmer that digs no bridge carries food across the water, to the pile that pays best per tick of its own trip.
// The Swimmer is given here (the way to one is AI17); one seat plays in each match.
#include <map>
#include <vector>

#include "ai_test.hpp"
#include "ants_sim/movement_tables.hpp"
#include "island_fixture.hpp"

namespace {

using namespace island_test;
using namespace ants;
using namespace ants::ai;

/// The tiles of the map that are open water now (a lake: terrain class water)
std::vector<sim::TileCoord> lake_tiles(const sim::SimulationEngine& sim) {
    std::vector<sim::TileCoord> out;
    const sim::Grid& grid = sim.grid();
    for (int y = 0; y < static_cast<int>(grid.height()); ++y) {
        for (int x = 0; x < static_cast<int>(grid.width()); ++x) {
            if (grid.terrain_class_at(sim::TileCoord{x, y}) == sim::movement::kTerrainWater) out.push_back(sim::TileCoord{x, y});
        }
    }
    return out;
}

/// A rock or a reed in every tile of the lake (an object footprint on water: the engine lets no ant in, a Swimmer included) and back
void make_lake_solid(sim::SimulationEngine& sim, const std::vector<sim::TileCoord>& lake, bool solid) {
    for (const sim::TileCoord& t : lake) sim.grid_mut().get_cell_mut(static_cast<uint32_t>(t.x), static_cast<uint32_t>(t.y)).static_solid = solid;
}

}  // namespace

void run_island_ferry_tests() {
    TEST_CASE("AI18.1 A Swimmer Carries Food Across The Water With No Bridge At All (A Medium Bot, ISLANDS, Seat 0, One Given Swimmer, No Builder): It Is Sent To A Pile Before Tick 100, The Seat Scores Within 1,200 Ticks, A Swimmer Holds Food At Some Tick, And Nobody Digs Or Drowns")
    {
        Match m;
        m.ferry = true;
        m.init("ISLANDS", 1, 0x01, Level::Medium, 0x01, [](LevelPlan& p) { p.island_builders = 0; });
        uint64_t first_score = 0;
        bool carried = false;
        uint64_t first_sent = 0;
        for (int t = 1; t <= 4000; ++t) {
            m.tick();
            if (first_sent == 0 && m.bot(0)->ferry().sent() > 0) first_sent = static_cast<uint64_t>(t);
            if (first_score == 0 && m.sim.get_player_score(0) > 0) first_score = static_cast<uint64_t>(t);
            for (const auto& a : m.sim.get_world_state().ants) carried = carried || (a.player_id == 0 && a.type == sim::AntType::Swimmer && a.is_holding);
        }
        ASSERT_TRUE(first_sent > 0 && first_sent <= 100);
        ASSERT_TRUE(first_score > 0 && first_score <= 1200);
        ASSERT_TRUE(carried);
        ASSERT_TRUE(m.sim.get_player_score(0) >= 100);
        ASSERT_EQ(m.island(0).tiles_ordered(), 0u);
        ASSERT_EQ(m.total_unforced(), 0);
        ASSERT_EQ(m.bot(0)->ferry().failures(), 0u);
    } TEST_END();

    TEST_CASE("AI18.2 The Swimmer That Digs Is Not Taken For The Ferry, The Second One Is: With One Builder And Two Swimmers The Island Task Holds One And The Ferry Works The Other; With One Swimmer The Island Task Has It And The Ferry Has Nobody")
    {
        for (const uint32_t swimmers : {1u, 2u}) {
            Match m;
            m.ferry = true;
            m.init("ISLANDS", 1, 0x01, Level::Medium, 0x01);
            if (swimmers == 2) m.sim.spawn_unit(0, sim::AntType::Swimmer, m.ctl->map().hill(0).starts[0]);
            m.run(600);
            ASSERT_EQ(m.bot(0)->ferry().working(), swimmers - 1);
            ASSERT_TRUE(m.island(0).tiles_ordered() >= 1);
        }
    } TEST_END();

    TEST_CASE("AI18.3 The Ferry Pays: A Medium Bot With A Builder And A Ferry Swimmer Scores More Over 9,000 Ticks Than The Same Bot With The Builder Alone (ISLANDS, Seat 0, Seed 1), And Loses No Ant")
    {
        uint32_t refused = 0, asides = 0;
        const auto play = [&](bool ferry, int* lost) {
            Match m;
            m.ferry = ferry;
            m.init("ISLANDS", 1, 0x01, Level::Medium, 0x01);
            m.sim.spawn_unit(0, sim::AntType::Swimmer, m.ctl->map().hill(0).starts[0]);
            m.run(9000);
            *lost = m.total_unforced();
            refused += m.ctl->stats(0).filtered;
            asides += m.island(0).asides();
            return m.sim.get_player_score(0);
        };
        int lost_with = 0, lost_without = 0;
        const int32_t with = play(true, &lost_with);
        const int32_t without = play(false, &lost_without);
        ASSERT_TRUE(with > without + 200);
        ASSERT_EQ(lost_with + lost_without, 0);
        ASSERT_EQ(refused, 0u);                                                                                  // (the builder rests on the tile it digs next: it steps aside, no order is refused: AI15.16)
        ASSERT_TRUE(asides >= 1);
    } TEST_END();

    TEST_CASE("AI18.4 The Trip Model Of The Ferry Is The Engine's: The First Round Trip Of A Swimmer To A Pile (From The Order To The Deposit) Takes The Ticks That The Search Predicted, Within 20 Percent Under And 40 Over (The Delay Of The Order Is In It; ISLANDS, Seat 0)")
    {
        Match m;
        m.ferry = true;
        m.init("ISLANDS", 1, 0x01, Level::Medium, 0x01, [](LevelPlan& p) { p.island_builders = 0; });
        uint64_t sent = 0, first_score = 0;
        uint32_t pile = 0;
        for (int t = 1; t <= 2500 && first_score == 0; ++t) {
            m.tick();
            if (sent == 0 && m.bot(0)->ferry().sent() > 0) {
                sent = static_cast<uint64_t>(t);
                for (const auto& a : m.sim.get_world_state().ants) {
                    if (a.player_id == 0 && a.type == sim::AntType::Swimmer) m.bot(0)->ferry().assigned_pile(a.id, pile);
                }
            }
            if (first_score == 0 && m.sim.get_player_score(0) > 0) first_score = static_cast<uint64_t>(t);
        }
        ASSERT_TRUE(sent > 0 && first_score > sent);
        const int32_t predicted = m.bot(0)->ferry().trip_ticks(pile);
        ASSERT_TRUE(predicted > 0);
        const int64_t measured = static_cast<int64_t>(first_score) - static_cast<int64_t>(sent);
        ASSERT_TRUE(measured * 100 >= static_cast<int64_t>(predicted) * 80 && measured * 100 <= static_cast<int64_t>(predicted) * 140);       // (the way there and back, a bite, the gate, and the delay of the order)
    } TEST_END();

    TEST_CASE("AI18.5 The Ferry Is Idle Where There Is Nothing To Ferry: TINY, MEDIUM, GAUNTLET And TREASURE At Every Level Send No Swimmer; A Bot Without A Swimmer Sends Nobody")
    {
        for (const char* map : {"TINY", "MEDIUM", "GAUNTLET", "TREASURE"}) {
            for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
                Match m;
                m.ferry = true;
                m.init(map, 3, 0x01, level, 0x01);                                                    // (even with a Swimmer in the hand: nothing lies beyond water)
                m.run(1500);
                ASSERT_EQ(m.bot(0)->ferry().working(), 0u);
            }
        }
        Match none;
        none.ferry = true;
        none.init("ISLANDS", 1, 0x01, Level::Medium, 0);
        none.run(300);
        ASSERT_EQ(none.bot(0)->ferry().sent(), 0u);
    } TEST_END();

    TEST_CASE("AI18.6 At Most Two Swimmers Work One Pile (The Hill Banks About One Deposit In A Hundred Ticks, And The Workers Use The Gate As Well): A Medium Bot With Three Swimmers And No Builder (ISLANDS, Seat 0) Has All Three Sent Within 200 Ticks, Two To The Best Pile And One To Another")
    {
        Match m;
        m.ferry = true;
        m.init("ISLANDS", 1, 0x01, Level::Medium, 0x01, [](LevelPlan& p) { p.island_builders = 0; });
        for (int i = 0; i < 2; ++i) m.sim.spawn_unit(0, sim::AntType::Swimmer, m.ctl->map().hill(0).starts[0]);
        m.run(200);
        std::map<uint32_t, int> per_pile;
        int sent = 0;
        for (const auto& a : m.sim.get_world_state().ants) {
            uint32_t pile = 0;
            if (a.player_id == 0 && a.type == sim::AntType::Swimmer && m.bot(0)->ferry().assigned_pile(a.id, pile)) {
                ++per_pile[pile];
                ++sent;
            }
        }
        ASSERT_EQ(sent, 3);
        ASSERT_EQ(per_pile.size(), 2u);
        for (const auto& e : per_pile) ASSERT_TRUE(e.second <= 2);
    } TEST_END();

    TEST_CASE("AI18.7 An Order That Does Nothing Is Learned From (A Medium Bot, ISLANDS, Seat 0, One Given Swimmer): The First Order Of The Swimmer Is Lost On Its Way To The Engine; 160 Ticks After It Left The Swimmer Stands Where It Stood, Empty, And The Ferry Counts One Failure, Lets It Go, And Sends It To Another Pile, Not To The One That Did Not Take It, And That Order Works")
    {
        Match m;
        m.ferry = true;
        m.latency = 4;
        m.init("ISLANDS", 1, 0x01, Level::Medium, 0x01, [](LevelPlan& p) { p.island_builders = 0; });
        const uint32_t swimmer = m.swimmers[0];
        const auto orders_of_swimmer = [&] {
            size_t n = 0;
            for (const auto& e : m.log()) n += e.second.type == CommandType::GroupMove && e.second.ants.size() == 1 && e.second.ants[0] == swimmer ? 1u : 0u;
            return n;
        };
        int guard = 0;
        while (orders_of_swimmer() == 0 && guard++ < 300) m.tick();
        ASSERT_EQ(orders_of_swimmer(), 1u);
        uint32_t first_pile = 0;
        ASSERT_TRUE(m.bot(0)->ferry().assigned_pile(swimmer, first_pile));
        ASSERT_TRUE(m.delayed != nullptr);
        m.delayed->drop_pending();                                                                           // (the bot has been told that the order left: the engine never hears of it)
        const uint64_t left = m.sim.current_tick();
        m.run(120);
        ASSERT_EQ(m.bot(0)->ferry().failures(), 0u);                                                         // (not yet: the order left 120 ticks ago)
        m.run(static_cast<int>(left + 250 - m.sim.current_tick()));
        ASSERT_EQ(m.bot(0)->ferry().failures(), 1u);
        ASSERT_EQ(m.bot(0)->ferry().sent(), 2u);
        uint32_t second_pile = 0;
        ASSERT_TRUE(m.bot(0)->ferry().assigned_pile(swimmer, second_pile));
        ASSERT_TRUE(second_pile != first_pile);                                                              // (the pile that did not take it is left alone for it for 900 ticks)
        m.run(1500);
        ASSERT_EQ(m.bot(0)->ferry().failures(), 1u);                                                         // (the second order reached the engine and the Swimmer works)
        ASSERT_TRUE(m.sim.get_player_score(0) > 0);
        ASSERT_EQ(m.total_unforced(), 0);
    } TEST_END();

    TEST_CASE("AI18.8 A Swimmer That Stands Idle With Its Food Is Sent Home Whether Or Not A Pile Is Its Work (A Medium Bot, ISLANDS, Seat 0, One Given Swimmer): With The Ring Of Fire Walls Round The Hill The Engine Finds No Way Home And The Swimmer Stands With Its Food; After 900 Ticks The Ferry Sends It To The Entrance, It Banks The Food, Its Pile Is No Longer Its Work, And It Is Sent Again")
    {
        Match m;
        m.ferry = true;
        m.init("ISLANDS", 1, 0x01, Level::Medium, 0x01, [](LevelPlan& p) { p.island_builders = 0; });
        const uint32_t swimmer = m.swimmers[0];
        const auto holding = [&] {
            for (const auto& a : m.sim.get_world_state().ants) {
                if (a.id == swimmer) return a.is_holding;
            }
            return false;
        };
        int guard = 0;
        while (!holding() && guard++ < 3000) m.tick();
        ASSERT_TRUE(holding());
        const auto ring = SabotageTask::ring_of(m.ctl->map().hill(0));
        for (const sim::TileCoord& t : ring) m.sim.grid_mut().place_firewall(static_cast<uint32_t>(t.x), static_cast<uint32_t>(t.y), 1);
        m.run(400);
        ASSERT_TRUE(holding());                                                                              // (it could not bank it)
        uint32_t pile = 0;
        ASSERT_TRUE(m.bot(0)->ferry().assigned_pile(swimmer, pile));                                        // (the pile still exists: the Swimmer has its record)
        ASSERT_EQ(m.bot(0)->ferry().rescues(), 0u);
        for (const sim::TileCoord& t : ring) m.sim.grid_mut().clear_firewall(static_cast<uint32_t>(t.x), static_cast<uint32_t>(t.y));
        const int32_t before = m.sim.get_player_score(0);
        guard = 0;
        while (m.bot(0)->ferry().rescues() == 0 && guard++ < 2000) m.tick();
        ASSERT_EQ(m.bot(0)->ferry().rescues(), 1u);
        const uint64_t rescued_at = m.sim.current_tick();
        guard = 0;
        while (holding() && guard++ < 600) {                                                                 // (on its way home with its food the Swimmer still has its record: it ends when the food is banked)
            ASSERT_EQ(m.bot(0)->ferry().working(), 1u);
            m.tick();
        }
        ASSERT_TRUE(!holding());
        m.run(1800);
        ASSERT_TRUE(m.sim.get_player_score(0) >= before + 60);                                               // (it banked what it carried and went on working: the engine's loop remembers the pile)
        ASSERT_EQ(m.bot(0)->ferry().working(), 0u);                                                          // (its record ended with the rescue: it is no longer counted at the pile)
        ASSERT_EQ(m.bot(0)->ferry().failures(), 0u);                                                         // (and the empty Swimmer by the hill was not read as an order that did nothing)
        // the second time that it stands with its food (more than 900 ticks after the first rescue) the clock of 900 ticks starts anew: it does not run on from the first time
        guard = 0;
        while ((m.sim.current_tick() < rescued_at + 950 || !holding()) && guard++ < 3000) m.tick();
        ASSERT_TRUE(holding());
        for (const sim::TileCoord& t : ring) m.sim.grid_mut().place_firewall(static_cast<uint32_t>(t.x), static_cast<uint32_t>(t.y), 1);
        m.run(600);
        ASSERT_TRUE(holding());
        ASSERT_EQ(m.bot(0)->ferry().rescues(), 1u);
        m.run(700);
        ASSERT_EQ(m.bot(0)->ferry().rescues(), 2u);
        ASSERT_EQ(m.total_unforced(), 0);
    } TEST_END();

    TEST_CASE("AI18.9 A Lake That Is Not Open Water Is No Way For A Swimmer (A Rock Or A Reed In Every Tile: The Engine Lets No Ant Into A Tile With An Object): The Ferry Of A Medium Bot On ISLANDS, Whose Map Was Analysed With The Lake Open, Is Given A Swimmer After The Rocks Came And Finds No Way (No Trip To Any Pile), Sends Nobody And Counts No Failure, Where With The Lake Open It Sends Within 100 Ticks; A Map With The Rocks From The Start Has No Island To Serve")
    {
        // 0: the rocks come after the bot has analysed the map, and the Swimmer after the rocks (the ferry's own look at the water is what is tested); 1: the same with the lake open (the premise: the Swimmer is sent
        // within 100 ticks); 2: the rocks are there from the start (the analysis finds no water to cross: the ferry is not at work, and sends nobody all the same)
        for (int arm = 0; arm < 3; ++arm) {
            Match m;
            m.ferry = true;
            m.init("ISLANDS", 1, 0x01, Level::Medium, 0x00, [](LevelPlan& p) { p.island_builders = 0; },
                   [&](sim::SimulationEngine& sim) { if (arm == 2) make_lake_solid(sim, lake_tiles(sim), true); });
            m.run(150);
            ASSERT_EQ(m.island(0).active(), arm != 2);
            if (arm == 0) make_lake_solid(m.sim, lake_tiles(m.sim), true);
            m.sim.spawn_unit(0, sim::AntType::Swimmer, m.ctl->map().hill(0).starts[0]);
            m.run(arm == 1 ? 100 : 600);
            int32_t known = 0;
            for (uint32_t pile = 0; pile < 64; ++pile) known += m.bot(0)->ferry().trip_ticks(pile) >= 0 ? 1 : 0;
            if (arm == 1) {
                ASSERT_TRUE(m.bot(0)->ferry().sent() >= 1);
                ASSERT_TRUE(known >= 1);
                continue;
            }
            ASSERT_EQ(m.bot(0)->ferry().sent(), 0u);
            ASSERT_EQ(m.bot(0)->ferry().failures(), 0u);
            ASSERT_EQ(known, 0);
        }
    } TEST_END();
}
