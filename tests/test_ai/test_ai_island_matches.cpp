// AI16: whole matches of four standard bots on ISLANDS and SMALL, every level, the bots as they play (the expedition, the bridges and the ferry). The pieces are pinned by AI14 to AI19 in test_ai;
// here the whole is looked at: nobody is lost to a bridge or a flight, every team scores, the island of SMALL is taken, and the run is the same every time.
#include <cstdio>

#include "ai_test.hpp"
#include "island_fixture.hpp"

namespace {

using namespace island_test;
using namespace ants;
using namespace ants::ai;

struct Result {
    int32_t score[4]{0, 0, 0, 0};
    uint32_t swimmers_taken[4]{0, 0, 0, 0};          // Swimmers that the expedition of the seat took from a row of tokens
    int collapsed{0};
    int unforced{0};                     // ants that drowned for a reason of their own bot's making (not the blows of an enemy)
    int fought{0};
    uint64_t beyond_total{0};
    uint64_t beyond_left{0};
    uint64_t hash{0};
    uint64_t ticks{0};
};

Result play_match(const std::string& map, Level level, uint32_t seed) {
    Match m;
    m.expedition = true;
    m.ferry = true;
    m.latency = 3;                                                                                   // (the arena's default: what the tournaments and a room play with)
    m.init(map, seed, 0x0F, level, 0);
    while (!m.sim.is_match_over() && m.sim.current_tick() < 40000) m.tick();
    Result r;
    for (uint8_t t = 0; t < 4; ++t) {
        r.score[t] = m.sim.get_player_score(t);
        r.swimmers_taken[t] = m.bot(t)->expedition().swimmers_taken();
        r.fought += m.fought[t];
    }
    r.collapsed = m.total_collapsed();
    r.unforced = m.total_unforced();
    const MapInfo& info = m.ctl->map();
    const auto& objects = m.sim.grid().food_objects();
    for (const PileInfo& p : info.piles()) {
        bool walkers = false;
        for (uint8_t t = 0; t < 4; ++t) walkers = walkers || p.approach[t].reachable();
        if (walkers) continue;                                                                       // the points that no hill walks to: beyond the water
        r.beyond_total += static_cast<uint64_t>(p.units) * p.value;
        if (p.index < objects.size()) r.beyond_left += static_cast<uint64_t>(objects[p.index].remaining) * p.value;
    }
    r.hash = m.sim.state_hash().total;
    r.ticks = m.sim.current_tick();
    return r;
}

}  // namespace

void run_island_match_tests() {
    TEST_CASE("AI16.1 ISLANDS, Four Bots, Whole Matches (Seeds 1, 2 And 4, Every Level): Nobody Is Lost To A Bridge Or A Flight, Every Team Takes A Swimmer From A Row And Scores, And The Bots Take A Third Of The Points Beyond The Water At Medium And Hard")
    {
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            uint64_t taken = 0, total = 0;
            for (const uint32_t seed : {1u, 2u, 4u}) {
                const Result r = play_match("ISLANDS", level, seed);
                std::printf("    [ISLANDS level %d seed %u: scores %d %d %d %d, taken %llu of %llu, collapsed %d, lost to enemies %d, other %d]\n", static_cast<int>(level), seed, r.score[0], r.score[1], r.score[2], r.score[3], static_cast<unsigned long long>(r.beyond_total - r.beyond_left), static_cast<unsigned long long>(r.beyond_total), r.collapsed, r.fought, r.unforced - r.collapsed);
                ASSERT_EQ(r.collapsed, 0);
                ASSERT_EQ(r.unforced, 0);
                for (uint8_t t = 0; t < 4; ++t) {
                    ASSERT_TRUE(r.score[t] > 0);
                    ASSERT_TRUE(r.swimmers_taken[t] >= 1);
                }
                taken += r.beyond_total - r.beyond_left;
                total += r.beyond_total;
            }
            if (level != Level::Easy) ASSERT_TRUE(taken * 3 >= total);
        }
    } TEST_END();

    TEST_CASE("AI16.2 SMALL, Four Bots, Whole Matches (Seeds 1 To 3, Medium And Hard): Nobody Is Lost To A Bridge, And At Least Half Of The Island's 1,000 Points Are Taken On Average")
    {
        for (const Level level : {Level::Medium, Level::Hard}) {
            uint64_t taken = 0, total = 0;
            for (uint32_t seed = 1; seed <= 3; ++seed) {
                const Result r = play_match("SMALL", level, seed);
                std::printf("    [SMALL level %d seed %u: scores %d %d %d %d, island taken %llu of %llu, collapsed %d, lost to enemies %d, other %d]\n", static_cast<int>(level), seed, r.score[0], r.score[1], r.score[2], r.score[3], static_cast<unsigned long long>(r.beyond_total - r.beyond_left), static_cast<unsigned long long>(r.beyond_total), r.collapsed, r.fought, r.unforced - r.collapsed);
                ASSERT_EQ(r.collapsed, 0);
                ASSERT_EQ(r.unforced, 0);
                taken += r.beyond_total - r.beyond_left;
                total += r.beyond_total;
            }
            ASSERT_TRUE(taken * 2 >= total);
        }
    } TEST_END();

    TEST_CASE("AI16.3 The Whole Is Deterministic: The Same Match Twice (ISLANDS, Medium, Seed 1) Ends In The Same State, And The Island Machinery Changes Nothing On The Maps Without Water At The End Of A Whole Match Either (TINY, Medium, Seed 1)")
    {
        const Result a = play_match("ISLANDS", Level::Medium, 1);
        const Result b = play_match("ISLANDS", Level::Medium, 1);
        ASSERT_TRUE(a.hash == b.hash);
        ASSERT_EQ(a.ticks, b.ticks);
        Match on, off;
        on.expedition = true;
        on.ferry = true;
        on.init("TINY", 1, 0x0F, Level::Medium, 0);
        off.init("TINY", 1, 0x0F, Level::Medium, 0, [](LevelPlan& p) { p.islands = false; p.island_expedition = false; p.island_ferry = false; });
        while (!on.sim.is_match_over() && on.sim.current_tick() < 40000) {
            on.tick();
            off.tick();
        }
        ASSERT_TRUE(on.sim.state_hash().total == off.sim.state_hash().total);
    } TEST_END();
}
