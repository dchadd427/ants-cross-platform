// AI19: the island machinery on SMALL and on the maps without water (docs/BOTS.md "Islands"). SMALL: the two Swimmers lie on the banks of the lake and every hill reaches them on foot, the flowers
// drop more; the island in the middle has 1,000 of the 4,000 points. The other four maps: nothing may change where nothing lies beyond water.
#include "ai_test.hpp"
#include "island_fixture.hpp"

namespace {

using namespace island_test;
using namespace ants;
using namespace ants::ai;

uint32_t swimmers_of(Match& m, uint8_t seat) {
    uint32_t n = 0;
    for (const auto& a : m.sim.get_world_state().ants) n += (a.player_id == seat && a.type == sim::AntType::Swimmer) ? 1u : 0u;
    return n;
}

}  // namespace

void run_island_small_tests() {
    TEST_CASE("AI19.1 A Swimmer That Lies Within A Walk Is Taken At Once: A Bot Alone On SMALL, On Every Seat At Every Level, Has A Swimmer Before Tick 400 And The Second One Before Tick 500 (The Two Of The Banks), And No Expedition Is Planned")
    {
        for (uint8_t seat = 0; seat < 4; ++seat) {
            for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
                Match m;
                m.expedition = true;
                m.ferry = true;
                m.init("SMALL", 1, static_cast<uint8_t>(1u << seat), level, 0);
                uint64_t first = 0, second = 0;
                for (int t = 1; t <= 600; ++t) {
                    m.tick();
                    const uint32_t n = swimmers_of(m, seat);
                    if (n >= 1 && first == 0) first = static_cast<uint64_t>(t);
                    if (n >= 2 && second == 0) second = static_cast<uint64_t>(t);
                }
                ASSERT_TRUE(first > 0 && first <= 400);
                ASSERT_TRUE(second > 0 && second <= 500);
                ASSERT_EQ(m.bot(seat)->expedition().planned(), 0u);
            }
        }
    } TEST_END();

    TEST_CASE("AI19.2 A Swimmer That The Flower Drops Is Taken By The Bot That Is Nearest To It: Four Medium Bots On SMALL, A Swimmer Power-Up Put Down Eight Tiles From The Hill Of Seat 3 After 3,000 Ticks Is Taken By Seat 3, Within 600 Ticks, And By Nobody Else")
    {
        Match m;
        m.ferry = true;
        m.init("SMALL", 1, 0x0F, Level::Medium, 0);
        m.run(3000);
        uint32_t before[4];
        for (uint8_t seat = 0; seat < 4; ++seat) before[seat] = swimmers_of(m, seat);
        m.sim.grid_mut().place_powerup(8, 8, 5);
        m.run(600);
        for (uint8_t seat = 0; seat < 4; ++seat) ASSERT_EQ(swimmers_of(m, seat), before[seat] + (seat == 3 ? 1u : 0u));
        ASSERT_FALSE(m.sim.grid().has_powerup_at(TileCoord{8, 8}));
    } TEST_END();

    TEST_CASE("AI19.2b The Same With The Flower Rules On (Medium Plays None As Shipped): The Swimmer That Is Put Down Eight Tiles From The Hill Of Seat 3 Is Still Taken By Seat 3 Within 600 Ticks (The Drops Of The Flowers In The Same Ticks Go To The Seats Whose Side They Are On: AI23)")
    {
        Match m;
        m.ferry = true;
        m.init("SMALL", 1, 0x0F, Level::Medium, 0, [](LevelPlan& p) { p.flower_sides = p.flower_fire = p.flower_recover = p.flower_recall = true; });
        m.run(3000);
        const uint32_t before = swimmers_of(m, 3);
        m.sim.grid_mut().place_powerup(8, 8, 5);
        m.run(600);
        ASSERT_EQ(swimmers_of(m, 3), before + 1u);
        ASSERT_FALSE(m.sim.grid().has_powerup_at(TileCoord{8, 8}));
    } TEST_END();

    TEST_CASE("AI19.3 Nothing Changes Where Nothing Lies Beyond Water: On TINY, MEDIUM, GAUNTLET And TREASURE Four Bots Of A Level With The Island Machinery On And Four With It Off Play The Same Match, Tick For Tick (The State Hash Of Every 20th Tick Is The Same), At Every Level")
    {
        for (const char* map : {"TINY", "MEDIUM", "GAUNTLET", "TREASURE"}) {
            for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
                Match on, off;
                on.expedition = true;
                on.ferry = true;
                on.init(map, 2, 0x0F, level, 0, [](LevelPlan& p) { p.islands = true; });
                off.init(map, 2, 0x0F, level, 0, [](LevelPlan& p) { p.islands = false; p.island_expedition = false; p.island_ferry = false; });
                bool same = true;
                for (int t = 1; t <= 2400 && same; ++t) {
                    on.tick();
                    off.tick();
                    if (t % 20 == 0) same = on.sim.state_hash().total == off.sim.state_hash().total;
                }
                ASSERT_TRUE(same);
            }
        }
    } TEST_END();

    TEST_CASE("AI19.4 The Ferry Serves The Pile That No Walker Reaches First: A Medium Bot On SMALL With One Swimmer Of Its Own Besides The Two Of The Banks Has Two Of The Three Sent To The Island's Pile (The Mainland Piles Are Nearer, Which Is Why The Rule Is There) Within 400 Ticks")
    {
        Match m;
        m.ferry = true;
        m.init("SMALL", 1, 0x01, Level::Medium, 0x01);
        m.run(400);
        const MapInfo& map = m.ctl->map();
        uint32_t island = ~0u;
        for (const PileInfo& p : map.piles()) {
            if (!p.approach[0].reachable()) island = p.index;
        }
        ASSERT_TRUE(island != ~0u);
        int there = 0, assigned = 0;
        for (const auto& a : m.sim.get_world_state().ants) {
            uint32_t pile = 0;
            if (a.player_id == 0 && a.type == sim::AntType::Swimmer && m.bot(0)->ferry().assigned_pile(a.id, pile)) {
                ++assigned;
                there += pile == island ? 1 : 0;
            }
        }
        ASSERT_TRUE(assigned >= 2);
        ASSERT_EQ(there, 2);
    } TEST_END();
}
