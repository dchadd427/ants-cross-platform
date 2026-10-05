// AI17: the expedition of the standard bot (island_expedition.hpp, docs/BOTS.md "Islands"): a crew of plain workers is flown over the water by bombs to the Swimmers in the corners of ISLANDS,
// and one worker takes one token of the row and walks out. One seat plays in each match here (a few thousand ticks); the whole matches of four seats are AI16 (suite 2.28).
#include <functional>
#include <map>

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

Run play(Level level, uint8_t seat, uint32_t seed, int ticks, const std::function<void(LevelPlan&)>& tweak = {}, uint32_t latency = 0) {
    Match m;
    m.latency = latency;
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
        ASSERT_TRUE(r.first_swimmer <= 1950);                                                        // (a dud is seen at once; by the timeout alone the first Swimmer comes at 2,200 instead of 1,800)
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
        std::map<uint32_t, int> idle_with_food;                                                          // (one count for each Swimmer; an idle Swimmer in water reads Swimming, not Idle)
        int longest = 0;
        for (int t = 1; t <= 9000; ++t) {
            m.tick();
            for (const auto& a : m.sim.get_world_state().ants) {
                if (a.player_id != 0 || a.type != sim::AntType::Swimmer) continue;
                int& run = idle_with_food[a.id];
                if (a.is_holding && (a.state == sim::UnitState::Idle || a.state == sim::UnitState::Swimming)) longest = std::max(longest, ++run);
                else run = 0;
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

    TEST_CASE("AI17.9 The Swimmer That Took Its Token Is Not Shut In: Four Hard Bots On ISLANDS (Seed 4, The Plan Alone, Commands At Once) All Score Within 6,000 Ticks; In This Match A Crew Ant That Stood In The First Steps From The End Of The Row Once Held The Swimmer Of Seat 3 There For Good (One Tile Wide), And The Seat Scored Nothing")
    {
        Match m;
        m.expedition = true;
        m.ferry = true;
        m.builders = 0;
        m.init("ISLANDS", 4, 0x0F, Level::Hard, 0);
        m.run(6000);
        for (uint8_t seat = 0; seat < 4; ++seat) {
            ASSERT_TRUE(m.sim.get_player_score(seat) > 0);
            ASSERT_TRUE(m.bot(seat)->expedition().swimmers_taken() >= 1);
        }
    } TEST_END();

    TEST_CASE("AI17.10 The Landing Stays Free When The Bot Is Slow: An Easy Bot On Seat 3 With The Arena's Command Latency Of 3 Ticks (Seed 40, 5,000 Ticks) Loses No Ant; Without The Look At Who Stands By The Landing When The Next Hop Is Ordered, An Ant Of The Crew Is Thrown Into The Water")
    {
        const Run r = play(Level::Easy, 3, 40, 5000, {}, 3);
        ASSERT_EQ(r.lost, 0);
        ASSERT_EQ(r.drowned, 0);
        ASSERT_TRUE(r.first_landing > 0);
    } TEST_END();

    TEST_CASE("AI17.11 The Crew Is Never Smaller Than crew_min: A Row That Needs Fewer Ants Than The Crew Is Set To (Seven For A Row That Takes Five, No Spare; Nine Plain Ants At The Hill) Is Planned At The First Look With Seven Ants, Not Refused At Every Look For Ever (A Row That Starts With A Swimmer Needs One Ant, And The Same Group Was Chosen Each Time)")
    {
        Match m;
        m.expedition = true;
        m.ferry = true;
        m.init("ISLANDS", 1, 0x01, Level::Medium, 0);
        for (int i = 0; i < 3; ++i) m.sim.spawn_unit(0, sim::AntType::Worker, m.ctl->map().hill(0).starts[0]);
        ExpeditionTask::Params p = m.bot(0)->expedition().params();
        p.crew_min = 7;
        p.crew_spare = 0;
        m.bot(0)->expedition_for_labs().set_params(p);
        m.run(5);
        ASSERT_EQ(m.bot(0)->expedition().planned(), 1u);
        ASSERT_EQ(m.bot(0)->expedition().crew_size(), 7u);
        m.run(400);
        ASSERT_EQ(m.bot(0)->expedition().planned(), 1u);                                                  // (one attempt, and it goes on: nothing was given up)
        ASSERT_EQ(m.bot(0)->expedition().given_up(), 0u);
        ASSERT_TRUE(m.bot(0)->expedition().planted() >= 1);
        ASSERT_EQ(ExpeditionTask::Params{}.crew_min, 3u);
    } TEST_END();

    TEST_CASE("AI17.12 An Attempt That Is Given Up Is Tried Again After Longer Each Time: With A Patience Of 100 Ticks Nothing Progresses (Medium, ISLANDS, Seat 0), The Pause After Each Give-Up Is 900 Ticks, Then 1,800, Then 3,600 (Four Attempts In 7,000 Ticks), Not 900 Every Time (Seven), Which Held The Crew And A Bomber For Two Thirds Of The Match")
    {
        Match m;
        m.expedition = true;
        m.ferry = true;
        m.init("ISLANDS", 1, 0x01, Level::Medium, 0);
        ExpeditionTask::Params p = m.bot(0)->expedition().params();
        p.stuck_ticks = 100;
        m.bot(0)->expedition_for_labs().set_params(p);
        std::vector<uint64_t> began;
        uint32_t seen = 0;
        for (int t = 1; t <= 7000; ++t) {
            m.tick();
            while (seen < m.bot(0)->expedition().planned()) {
                began.push_back(m.sim.current_tick());
                ++seen;
            }
        }
        ASSERT_EQ(began.size(), 4u);
        ASSERT_TRUE(began[0] <= 5);
        ASSERT_TRUE(began[1] - began[0] >= 900 && began[1] - began[0] <= 1200);                            // (100 ticks of patience and a pause of 900)
        ASSERT_TRUE(began[2] - began[1] >= 1800 && began[2] - began[1] <= 2400);                           // (then 1,800)
        ASSERT_TRUE(began[3] - began[2] >= 3600 && began[3] - began[2] <= 4200);                           // (then 3,600)
        ASSERT_EQ(m.bot(0)->expedition().given_up(), 3u);
        ASSERT_EQ(ExpeditionTask::Params{}.retry_ticks, 900u);
    } TEST_END();

    TEST_CASE("AI17.13 The Pause After An Attempt That Was Given Up Doubles Three Times And Then Stays: With A Base Of 100 Ticks It Is 100, 200, 400, 800 And From The Fourth Give-Up In A Row 800 Again, Not 1,600 (The Attempts Of One Match Are Few, Because Each Takes Tokens, So The Rule Is Tested As Such; AI17.12 Shows That The Task Uses It)")
    {
        const uint64_t expected[] = {100, 200, 400, 800, 800, 800, 800};
        for (uint32_t n = 1; n <= 7; ++n) ASSERT_EQ(ExpeditionTask::pause_after(100, n), expected[n - 1]);
        ASSERT_EQ(ExpeditionTask::pause_after(100, 0), 100u);                                                // (never asked: the count is at least one when a give-up has happened)
        ASSERT_EQ(ExpeditionTask::pause_after(900, 1), 900u);                                                // (the plans' base)
        ASSERT_EQ(ExpeditionTask::pause_after(900, 4), 7200u);
        ASSERT_EQ(ExpeditionTask::pause_after(900, 40), 7200u);
    } TEST_END();
}
