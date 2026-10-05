// AI17: the expedition of the standard bot (island_expedition.hpp, docs/BOTS.md "Islands"): a crew of plain workers is flown over the water by bombs to the Swimmers in the corners of ISLANDS,
// and one worker takes one token of the row and walks out. One seat plays in each match here (a few thousand ticks); the whole matches of four seats are AI16 (suite 2.28).
#include <functional>

#include "ai_test.hpp"
#include "island_fixture.hpp"

namespace {

using namespace island_test;
using namespace ants;
using namespace ants::ai;

// What the expedition of one seat did in a match of one bot on ISLANDS (the level's plan)
struct Run {
    uint64_t first_plant{0};
    uint64_t first_landing{0};
    uint64_t first_swimmer{0};
    uint64_t second_swimmer{0};
    uint64_t third_swimmer{0};
    uint32_t swimmers{0};
    uint32_t planned{0};
    uint32_t given_up{0};
    uint32_t duds{0};
    uint32_t planted{0};
    uint32_t hops{0};
    uint32_t chains{0};
    bool active_at_end{false};
    size_t crew_at_end{0};
    int lost{0};                         // ants of the seat that died or drowned (the ants of the start: nobody hatches here)
    int drowned{0};                      // ants of the seat that started to drown, whatever the reason
    int bad_flight{0};                   // looks at which a flight in use was no flight of the analysis, or its landing not one that the engine accepts
    std::vector<int32_t> route;
    uint64_t hash{0};
    int32_t score{0};
};

Run play(Level level, uint8_t seat, uint32_t seed, int ticks, const std::function<void(LevelPlan&)>& tweak = {}) {
    Match m;
    m.expedition = true;
    m.ferry = true;                                                                                  // (the bot as it plays: the Swimmers of the expedition go to work at once)
    m.init("ISLANDS", seed, static_cast<uint8_t>(1u << seat), level, 0, tweak);
    Run r;
    std::set<uint32_t> alive;
    for (const auto& a : m.sim.get_world_state().ants) {
        if (a.player_id == seat) alive.insert(a.id);
    }
    const IslandInfo& info = m.island(seat).info();
    for (int t = 1; t <= ticks; ++t) {
        m.tick();
        const ExpeditionTask& ex = m.bot(seat)->expedition();
        if (r.route.empty() && !ex.route().empty()) r.route = ex.route();
        uint32_t sw = 0;
        std::set<uint32_t> now;
        for (const auto& a : m.sim.get_world_state().ants) {
            if (a.player_id != seat) continue;
            now.insert(a.id);
            sw += a.type == sim::AntType::Swimmer ? 1u : 0u;
        }
        for (const uint32_t id : alive) r.lost += now.count(id) == 0 ? 1 : 0;
        alive = now;
        if (sw >= 1 && r.first_swimmer == 0) r.first_swimmer = static_cast<uint64_t>(t);
        if (sw >= 2 && r.second_swimmer == 0) r.second_swimmer = static_cast<uint64_t>(t);
        if (sw >= 3 && r.third_swimmer == 0) r.third_swimmer = static_cast<uint64_t>(t);
        r.swimmers = sw;
        for (size_t leg = 0; leg + 1 < r.route.size(); ++leg) {                                          // a flight in use is a flight of the analysis, with a landing that the engine accepts
            const Flight* f = ex.flight_of(leg);
            if (f == nullptr) continue;
            bool listed = false;
            for (const Flight* g : info.flights_between(r.route[leg], r.route[leg + 1])) listed = listed || (g->from == f->from && g->bomb == f->bomb && g->land == f->land);
            if (!listed || !flight_landing_ok(m.sim.grid(), f->land)) ++r.bad_flight;
        }
    }
    const ExpeditionTask& ex = m.bot(seat)->expedition();
    r.first_plant = ex.first_plant();
    r.first_landing = ex.first_landing();
    r.planned = ex.planned();
    r.given_up = ex.given_up();
    r.duds = ex.duds();
    r.planted = ex.planted();
    r.hops = ex.hops();
    r.chains = m.island(seat).chains_finished();
    r.active_at_end = ex.active();
    r.crew_at_end = ex.crew_size();
    r.drowned = m.collapsed[seat] + m.other_drowned[seat];
    r.hash = m.sim.state_hash().total;
    r.score = m.sim.get_player_score(seat);
    return r;
}

}  // namespace

void run_island_expedition_tests() {
    // The bounds are those of the measured runs of the four seats (Medium, seeds 1 to 6: the first bomb at tick 160 to 230, the first ant on the next island at 280 to 760, the first Swimmer at 1200 to
    // 2200, one bomb in five a dud) with a margin; docs/BOTS.md, "Islands".
    TEST_CASE("AI17.1 The Opening Hop (A Medium Bot, ISLANDS, Every Seat): The Expedition Is Planned At The First Look Over Two Flights, A Bomber Plants At The Shore Within 400 Ticks, The Flights In Use Are Flights Of The Analysis With A Landing The Engine Accepts, And The First Ant Is On The Next Island Within 900 Ticks")
    {
        for (uint8_t seat = 0; seat < 4; ++seat) {
            const Run r = play(Level::Medium, seat, 1, 1200);
            ASSERT_EQ(r.planned, 1u);
            ASSERT_TRUE(r.route.size() >= 3);
            ASSERT_TRUE(r.first_plant > 0 && r.first_plant <= 400);
            ASSERT_TRUE(r.first_landing > r.first_plant && r.first_landing <= 900);
            ASSERT_EQ(r.bad_flight, 0);
        }
    } TEST_END();

    TEST_CASE("AI17.2 A Swimmer After The Hop (A Medium Bot, ISLANDS, Every Seat, Seed 1): The First Swimmer Is Taken After The Landing And Within 2,400 Ticks, The Second Within 3,000; Over Two Flights (Seats 0 To 2) The Third (The Level Wants Three) Within 3,400, Then The Expedition Is Over And Its Ants Are Free; Over Three (Seat 3) The Bot Has Two At The End")
    {
        for (uint8_t seat = 0; seat < 4; ++seat) {
            const Run r = play(Level::Medium, seat, 1, 4000);
            ASSERT_TRUE(r.first_swimmer > r.first_landing && r.first_swimmer <= 2400);
            ASSERT_TRUE(r.second_swimmer >= r.first_swimmer && r.second_swimmer <= 3000);
            if (r.route.size() > 3) {                                                                // (a third from the far row takes all five ants of the crew through three hops, and a fifth of the bombs are duds)
                ASSERT_TRUE(r.swimmers >= 2);
                continue;
            }
            ASSERT_TRUE(r.third_swimmer >= r.second_swimmer && r.third_swimmer <= 3400);
            ASSERT_EQ(r.swimmers, 3u);
            ASSERT_FALSE(r.active_at_end);
            ASSERT_EQ(r.crew_at_end, 0u);
        }
    } TEST_END();

    TEST_CASE("AI17.3 No Ant Is Lost To The Flights: Over 3,600 Ticks At Medium, On Every Seat And Seeds 1 To 4 (A Fifth Of The Bombs Are Duds), No Ant Dies Or Drowns (An Ant Is Never Left On The Tile Where Another One Lands: The Engine Throws Both To A Neighbour Tile, Water Included), And The Expedition Gets Its Swimmers Each Time")
    {
        for (uint8_t seat = 0; seat < 4; ++seat) {
            for (uint32_t seed = 1; seed <= 4; ++seed) {
                const Run r = play(Level::Medium, seat, seed, 3600);
                ASSERT_EQ(r.lost, 0);
                ASSERT_EQ(r.drowned, 0);
                ASSERT_EQ(r.bad_flight, 0);
                ASSERT_TRUE(r.swimmers >= 1);
            }
        }
    } TEST_END();

    TEST_CASE("AI17.4 A Bomb That Is A Dud Is Planted Again: A Seed With Four Duds (Medium, Seat 0, Seed 2: Each Costs The Ant 2 Of Its 10 Hit Points, So An Ant Of The Crew Is Out Of Hit Points For The Next Flight) Still Lands The Crew, Takes A Swimmer, And Loses Nobody")
    {
        const Run r = play(Level::Medium, 0, 2, 3600);
        ASSERT_TRUE(r.duds >= 3);
        ASSERT_TRUE(r.planted >= r.hops);
        ASSERT_EQ(r.lost, 0);
        ASSERT_EQ(r.drowned, 0);
        ASSERT_TRUE(r.first_landing > 0 && r.first_swimmer > r.first_landing);
        ASSERT_TRUE(r.swimmers >= 1);
    } TEST_END();

    TEST_CASE("AI17.5 The Levels Want Their Own Number Of Swimmers: Easy Two, Medium Three, Hard Three (Seat 0, Seed 1); The Expedition Is Over When The Bot Has Them, And A Hard Bot Is Quicker Than A Medium One, And A Medium One Than An Easy One")
    {
        const Run easy = play(Level::Easy, 0, 1, 9000);
        const Run medium = play(Level::Medium, 0, 1, 9000);
        const Run hard = play(Level::Hard, 0, 1, 9000);
        ASSERT_EQ(easy.swimmers, 2u);
        ASSERT_EQ(medium.swimmers, 3u);
        ASSERT_EQ(hard.swimmers, 3u);
        ASSERT_TRUE(easy.first_swimmer > medium.first_swimmer && medium.first_swimmer > hard.first_swimmer);
        ASSERT_TRUE(easy.first_swimmer > 0 && easy.first_swimmer <= 6000);
        ASSERT_TRUE(easy.second_swimmer > 0 && easy.second_swimmer <= 7000);
        ASSERT_TRUE(hard.third_swimmer > 0 && hard.third_swimmer <= 2400);
        ASSERT_EQ(easy.lost + medium.lost + hard.lost, 0);
        ASSERT_EQ(easy.drowned + medium.drowned + hard.drowned, 0);
    } TEST_END();

    TEST_CASE("AI17.6 The Swimmers Go To Work: After The Expedition Of A Medium Bot (Seat 0, Seed 1) The Seat Scores From Its Swimmers (The Workers Are All In The Crew: Nobody Would Walk A Bridge, So None Is Dug), 1,000 By Tick 9,000, And Not One Swimmer Idles With Food For 900 Ticks")
    {
        Match m;
        m.expedition = true;
        m.ferry = true;
        m.init("ISLANDS", 1, 0x01, Level::Medium, 0);
        int idle_with_food = 0, longest = 0;
        for (int t = 1; t <= 9000; ++t) {
            m.tick();
            for (const auto& a : m.sim.get_world_state().ants) {
                if (a.player_id != 0 || a.type != sim::AntType::Swimmer) continue;
                if (a.is_holding && a.state == sim::UnitState::Idle) longest = std::max(longest, ++idle_with_food);
                else idle_with_food = 0;
            }
        }
        ASSERT_TRUE(m.sim.get_player_score(0) >= 1000);
        ASSERT_EQ(m.island(0).tiles_ordered(), 0u);
        ASSERT_TRUE(longest < 900);
        ASSERT_EQ(m.total_unforced(), 0);
    } TEST_END();

    TEST_CASE("AI17.7 The Expedition Is Idle Where Nothing Lies Beyond Water That A Swimmer Is Needed For: TINY, MEDIUM, GAUNTLET, TREASURE And SMALL (Where The Swimmers Lie Within A Walk) At Every Level: Nothing Is Planned, No Bomb Is Planted, Nothing Is Claimed")
    {
        for (const char* map : {"TINY", "MEDIUM", "GAUNTLET", "TREASURE", "SMALL"}) {
            for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
                Match m;
                m.expedition = true;
                m.init(map, 3, 0x01, level, 0);
                m.run(1500);
                const ExpeditionTask& ex = m.bot(0)->expedition();
                ASSERT_EQ(ex.planned(), 0u);
                ASSERT_EQ(ex.planted(), 0u);
                ASSERT_EQ(ex.hops(), 0u);
                ASSERT_FALSE(ex.active());
                ASSERT_EQ(ex.crew_size(), 0u);
            }
        }
    } TEST_END();

    TEST_CASE("AI17.8 The Expedition Is Deterministic: The Same Match Twice Gives The Same State Hash At The End And The Same Times Of The Hop And Of The Swimmer")
    {
        const Run a = play(Level::Medium, 0, 5, 2600);
        const Run b = play(Level::Medium, 0, 5, 2600);
        ASSERT_TRUE(a.hash == b.hash);
        ASSERT_EQ(a.first_landing, b.first_landing);
        ASSERT_EQ(a.first_swimmer, b.first_swimmer);
        ASSERT_EQ(a.duds, b.duds);
    } TEST_END();
}
