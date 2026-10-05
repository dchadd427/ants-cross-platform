// AI18: the ferry of the standard bot (island_ferry.hpp, docs/BOTS.md "Islands"): a Swimmer that digs no bridge carries food across the water, to the pile that pays best per tick of its own trip.
// The Swimmer is given here (the way to one is AI17); one seat plays in each match.
#include <map>

#include "ai_test.hpp"
#include "island_fixture.hpp"

namespace {

using namespace island_test;
using namespace ants;
using namespace ants::ai;

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
}
