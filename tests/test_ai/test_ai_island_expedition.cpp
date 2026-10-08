// AI17: the expedition of the standard bot (island_expedition.hpp, docs/BOTS.md "Islands"): a crew of plain workers is flown over the water by bombs to the Swimmers in the corners of ISLANDS,
// and one worker takes one token of the row and walks out. One seat plays in each match here (a few thousand ticks); the whole matches of four seats are AI16 (suite 2.28).
#include <algorithm>
#include <functional>
#include <map>

#include "ai_test.hpp"
#include "ants_ai/arena.hpp"
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
    uint32_t burns{0};                   // ants of the seat that started to burn (the engine's duds)
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
    std::set<uint32_t> burning;
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
            if (a.state != sim::UnitState::Burn) burning.erase(a.id);
            else if (burning.insert(a.id).second) ++r.burns;
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

// A match of one bot on ISLANDS in which the first crew ant that waits on the shore tile of the first flight (idle there on two ticks in a row, and not one of those in line for a hop) is left with 2
// hit points at once, as if a bomb had hurt it before; then 3,500 more ticks are played
struct Stranded {
    uint32_t ant{0};
    uint64_t tick{0};                    // when it was left with 2 hit points (0: no ant waited on the tile)
    bool alive{false};
    uint32_t swimmers{0};                // Swimmers taken by the expedition, and tokens of any kind
    uint32_t taken{0};
    uint32_t given_up{0};
    int bombers_on_row{0};               // Bombers of the seat on the island of the row at the end
    uint32_t bomber{0};                  // the Bomber of the first flight that was left with 2 hit points once it had flown on and landed (0: none did)
};

Stranded strand(Level level, uint8_t seat, uint32_t seed, uint32_t latency = 0, bool hurt_bomber = false) {
    Match m;
    m.latency = latency;
    m.expedition = true;
    m.ferry = true;
    m.init("ISLANDS", seed, static_cast<uint8_t>(1u << seat), level, 0);
    const ExpeditionTask& ex = m.bot(seat)->expedition();
    Stranded r;
    std::set<uint32_t> in_line;                                                                      // the ants that stood on the tile when a hop was ordered
    std::map<uint32_t, int> waiting;                                                                 // ticks in a row that an ant has stood idle on it
    for (int t = 1; t <= 1500 && r.ant == 0; ++t) {
        const uint32_t hops = ex.hops();
        m.tick();
        const Flight* f = ex.flight_of(0);
        if (f == nullptr) continue;
        std::set<uint32_t> on_tile;
        for (const auto& a : m.sim.get_world_state().ants) {
            if (a.player_id == seat && a.raw_type == sim::AntType::Worker && a.tile_x == f->from.x && a.tile_y == f->from.y && a.state == sim::UnitState::Idle) on_tile.insert(a.id);
        }
        if (ex.hops() != hops) {
            in_line.insert(on_tile.begin(), on_tile.end());
            waiting.clear();
            continue;
        }
        std::map<uint32_t, int> now;
        for (const uint32_t id : on_tile) {
            if (in_line.count(id) != 0) continue;
            now[id] = waiting.count(id) != 0 ? waiting[id] + 1 : 1;
            if (now[id] >= 2) r.ant = id;
        }
        waiting = now;
    }
    if (r.ant == 0) return r;
    const int32_t row_island = ex.route().back();
    r.tick = m.sim.current_tick();
    m.sim.get_unit(r.ant).hp = 2;
    uint32_t bomber = 0;
    for (int t = 0; t < 3500; ++t) {
        m.tick();
        if (!hurt_bomber || r.bomber != 0) continue;
        if (ex.bomber_of(0) != 0) {
            bomber = ex.bomber_of(0);
        } else if (bomber != 0 && ex.active()) {                                                     // (the first flight has lost its Bomber while the expedition goes on: it flew on and landed)
            m.sim.get_unit(bomber).hp = 2;
            r.bomber = bomber;
        }
    }
    r.swimmers = ex.swimmers_taken();
    r.taken = ex.taken();
    r.given_up = ex.given_up();
    for (const auto& a : m.sim.get_world_state().ants) {
        if (a.player_id != seat) continue;
        if (a.id == r.ant) r.alive = true;
        if (a.raw_type == sim::AntType::Bomber && m.ctl->map().ant_component(seat, sim::TileCoord{a.tile_x, a.tile_y}) == row_island) ++r.bombers_on_row;
    }
    return r;
}

// A match of four bots on ISLANDS as the arena plays them (the level's plan and a style drawn for each seat; `latency` ticks for a command), watched on one seat for `ticks` ticks
struct Natural {
    uint64_t gave_up{0};                 // the tick at which the first attempt was given up (0: none was)
    uint64_t last_take{0};               // the tick of the last token taken before the first attempt was given up (or before the end)
    uint64_t wait{0};                    // the longest stretch in which no token was taken, once four were
    uint64_t third{0};                   // the tick at which the third Swimmer was taken (0: not within the time)
    uint32_t given_up{0};
    uint32_t swimmers{0};
    int bombers_on_row{0};               // Bombers of the seat on the island of the row at the end
};

Natural four_bots(Level level, uint32_t seed, uint32_t latency, uint8_t seat, int ticks) {
    Match m;
    m.latency = latency;
    m.styled = true;
    m.init("ISLANDS", seed, 0xFu, level, 0);
    const ExpeditionTask& ex = m.bot(seat)->expedition();
    Natural r;
    int32_t row = -2;
    uint32_t taken = 0;
    uint64_t since = 0;
    for (int t = 1; t <= ticks; ++t) {
        m.tick();
        if (row == -2 && !ex.route().empty()) row = ex.route().back();
        if (r.gave_up == 0 && ex.given_up() > 0) r.gave_up = static_cast<uint64_t>(t);
        if (r.third == 0 && ex.swimmers_taken() >= 3) r.third = static_cast<uint64_t>(t);
        if (ex.taken() == taken) continue;
        if (taken >= 4) r.wait = std::max(r.wait, static_cast<uint64_t>(t) - since);
        taken = ex.taken();
        since = static_cast<uint64_t>(t);
        if (r.gave_up == 0) r.last_take = since;
    }
    r.given_up = ex.given_up();
    r.swimmers = ex.swimmers_taken();
    for (const auto& a : m.sim.get_world_state().ants) {
        if (a.player_id == seat && a.raw_type == sim::AntType::Bomber && m.ctl->map().ant_component(seat, sim::TileCoord{a.tile_x, a.tile_y}) == row) ++r.bombers_on_row;
    }
    return r;
}

}  // namespace

void run_island_expedition_tests() {
    // The bounds are those of the runs of the four seats (Medium, seed 1) with a margin; docs/BOTS.md, "Islands".
    TEST_CASE("AI17.1 The Opening Hop (A Medium Bot, ISLANDS, Every Seat): The Expedition Is Planned At The First Look Over Two Flights (Seat 3 Too: Its Short Way Goes Over A Satellite With No Bomber To Take, And The Bomber Of The Hill Flies On With The Crew), A Bomber Plants At The Shore Within 400 Ticks, The Flights In Use Are Flights Of The Analysis With A Landing The Engine Accepts, And The First Ant Is On The Next Island Within 900 Ticks")
    {
        for (uint8_t seat = 0; seat < 4; ++seat) {
            const Run r = play(Level::Medium, seat, 1, 1200);
            ASSERT_EQ(r.planned, 1u);
            ASSERT_EQ(r.route.size(), 3u);
            ASSERT_TRUE(r.first_plant > 0 && r.first_plant <= 400);
            ASSERT_TRUE(r.first_landing > r.first_plant && r.first_landing <= 900);
            ASSERT_EQ(r.bad_flight, 0);
        }
    } TEST_END();

    TEST_CASE("AI17.2 A Swimmer After The Hop (A Medium Bot, ISLANDS, Every Seat, Seed 1): The First Swimmer Is Taken After The Landing And Within 2,400 Ticks, The Second Within 3,000, The Third (The Level Wants Three) Within 3,400, Then The Expedition Is Over And Its Ants Are Free; On Seat 3 The Bomber Had To Fly On, For The Island Where The Second Flight Starts Has None To Take")
    {
        for (uint8_t seat = 0; seat < 4; ++seat) {
            const Run r = play(Level::Medium, seat, 1, 4000);
            ASSERT_TRUE(r.first_swimmer > r.first_landing && r.first_swimmer <= 2400);
            ASSERT_TRUE(r.second_swimmer >= r.first_swimmer && r.second_swimmer <= 3000);
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

    TEST_CASE("AI17.4 A Bomb That Is A Dud Is Planted Again, And The Dud Is Seen As Soon As The Ant Burns: A Hard Bot With The Arena's Latency Of 3 Ticks (Seat 2, Seed 5, Four Duds, Each Costs The Ant 2 Of Its 10 Hit Points) Still Lands The Crew, Has Its Three Swimmers By Tick 1,400 And Loses Nobody (Waiting 60 Ticks To See The Ant Still On B Makes It 1,586)")
    {
        const Run r = play(Level::Hard, 2, 5, 2400, {}, 3);
        ASSERT_TRUE(r.duds >= 4);
        ASSERT_TRUE(r.planted >= r.hops);
        ASSERT_EQ(r.lost, 0);
        ASSERT_EQ(r.drowned, 0);
        ASSERT_TRUE(r.first_landing > 0 && r.first_swimmer > r.first_landing);
        ASSERT_EQ(r.swimmers, 3u);
        ASSERT_TRUE(r.third_swimmer > 0 && r.third_swimmer <= 1400);
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

    TEST_CASE("AI17.12 An Attempt That Is Given Up Is Tried Again After Longer Each Time: With A Patience Of 30 Ticks Nothing Progresses (Medium, ISLANDS, Seat 0: The First Bomb Takes Longer To Plant), The Pause After Each Give-Up Is 900 Ticks, Then 1,800, Then 3,600 (Four Attempts In 7,000 Ticks), Not 900 Every Time (Seven), Which Held The Crew And A Bomber For Two Thirds Of The Match")
    {
        Match m;
        m.expedition = true;
        m.ferry = true;
        m.init("ISLANDS", 1, 0x01, Level::Medium, 0);
        ExpeditionTask::Params p = m.bot(0)->expedition().params();
        p.stuck_ticks = 30;
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
        ASSERT_TRUE(began[1] - began[0] >= 900 && began[1] - began[0] <= 1200);                            // (30 ticks of patience and a pause of 900)
        ASSERT_TRUE(began[2] - began[1] >= 1800 && began[2] - began[1] <= 2400);                           // (then 1,800)
        ASSERT_TRUE(began[3] - began[2] >= 3600 && began[3] - began[2] <= 4200);                           // (then 3,600)
        ASSERT_TRUE(m.bot(0)->expedition().given_up() >= 3u);                                              // (the fourth, begun at about 6,400, is given up 30 ticks later)
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

    TEST_CASE("AI17.14 The Arena Reports The Expedition As The Task Counts It: Every Field Of ExpeditionResult Is The Task's Own Counter (A Hard Bot, Seat 0, Seed 6, A Match In Which No Two Of The Counters Are Equal, So That A Swap Shows), And The Ticks Come In The Order Of The Play")
    {
        Match m;
        m.expedition = true;
        m.ferry = true;
        m.init("ISLANDS", 6, 0x01, Level::Hard, 0);
        m.run(3600);
        const ExpeditionTask& ex = m.bot(0)->expedition();
        const ExpeditionResult e = read_expedition(ex);
        ASSERT_EQ(e.planned, ex.planned());
        ASSERT_EQ(e.given_up, ex.given_up());
        ASSERT_EQ(e.planted, ex.planted());
        ASSERT_EQ(e.hops, ex.hops());
        ASSERT_EQ(e.landings, ex.landings());
        ASSERT_EQ(e.duds, ex.duds());
        ASSERT_EQ(e.taken, ex.taken());
        ASSERT_EQ(e.swimmers_taken, ex.swimmers_taken());
        ASSERT_TRUE(e.first_plant == ex.first_plant() && e.first_landing == ex.first_landing() && e.first_swimmer == ex.first_swimmer());
        ASSERT_TRUE(e.planted > e.hops && e.hops > e.landings && e.landings > e.taken && e.taken > e.duds && e.duds > e.swimmers_taken && e.swimmers_taken > e.planned && e.planned > e.given_up);
        ASSERT_TRUE(0 < e.first_plant && e.first_plant < e.first_landing && e.first_landing < e.first_swimmer);
    } TEST_END();

    // The tests below pin single matches of the arena's tables (Hard with its command latency of 3 ticks, Medium with none); with the rule left out, the numbers in brackets are what the same match gives.
    TEST_CASE("AI17.15 Easy Does Not Fly The Bomber On, Medium And Hard Do: The Plans Say So, And On Seat 3 (Whose Short Way Goes Over A Satellite With No Bomber To Take) The Route Is Two Flights At Medium And Hard And The Old Three At Easy, Which Has A Bomber To Take On Every Island")
    {
        ASSERT_FALSE(plan_for(Level::Easy).island_fly_on);
        ASSERT_TRUE(plan_for(Level::Medium).island_fly_on);
        ASSERT_TRUE(plan_for(Level::Hard).island_fly_on);
        ASSERT_TRUE(LevelPlan{}.island_fly_on);                                                      // (a plan made by hand flies it on)
        ASSERT_TRUE(ExpeditionTask::Params{}.fly_on);
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            const Run r = play(level, 3, 1, 150);
            ASSERT_EQ(r.planned, 1u);
            ASSERT_EQ(r.route.size(), level == Level::Easy ? 4u : 3u);
        }
    } TEST_END();

    TEST_CASE("AI17.16 The Second Order To An Ant Comes As Soon As The Latency Of The Level Allows: A Hard Bot With The Arena's Latency Of 3 Ticks (Seat 1, Seed 19, Five Duds) Has Its Three Swimmers By Tick 1,700 (A Fixed Gap Of 60 Ticks: 2,122) And Loses Nobody")
    {
        const Run r = play(Level::Hard, 1, 19, 2400, {}, 3);
        ASSERT_TRUE(r.duds >= 5);
        ASSERT_EQ(r.lost, 0);
        ASSERT_EQ(r.swimmers, 3u);
        ASSERT_TRUE(r.third_swimmer > 0 && r.third_swimmer <= 1700);
    } TEST_END();

    TEST_CASE("AI17.17 The Next Hop Does Not Wait For An Ant That Walks Off The Landing: A Medium Bot (Seat 2, Seed 7) Has Its First Swimmer By Tick 1,500 (Waiting For Every Ant Within Four Tiles Of The Landing: 1,760; For Every Ant On It, Walking Or Not: 2,111); The Safety Of It Is AI17.3 And AI17.10")
    {
        const Run r = play(Level::Medium, 2, 7, 2400);
        ASSERT_EQ(r.lost, 0);
        ASSERT_TRUE(r.first_swimmer > 0 && r.first_swimmer <= 1500);
        ASSERT_TRUE(r.third_swimmer > 0 && r.third_swimmer <= 2000);
    } TEST_END();

    TEST_CASE("AI17.18 The Bomber Flies On Only When The Crew Is Over: A Hard Bot With The Arena's Latency Of 3 Ticks On Seat 3 (Seed 19, Six Duds) Has Its First Swimmer By Tick 2,400 And Its Third By 2,900 (A Bomber That Flies On While Crew Ants Are To Come, And Leaves Them Behind: 2,770 and 3,097)")
    {
        const Run r = play(Level::Hard, 3, 19, 3400, {}, 3);
        ASSERT_EQ(r.lost, 0);
        ASSERT_EQ(r.swimmers, 3u);
        ASSERT_TRUE(r.first_swimmer > 0 && r.first_swimmer <= 2400);
        ASSERT_TRUE(r.third_swimmer > 0 && r.third_swimmer <= 2900);
    } TEST_END();

    TEST_CASE("AI17.19 The Bomber That Flies On Plants Before It Walks To The Shore: A Medium Bot On Seat 3 Has Its Third Swimmer By Tick 3,010 (Seed 10) And 3,450 (Seed 20); Sent To The Shore At Once, The Bomber Comes Back To Plant And The Hop Is Late By About 130 Ticks")
    {
        const Run a = play(Level::Medium, 3, 10, 3600);
        ASSERT_TRUE(a.third_swimmer > 0 && a.third_swimmer <= 3010);
        const Run b = play(Level::Medium, 3, 20, 3800);
        ASSERT_TRUE(b.third_swimmer > 0 && b.third_swimmer <= 3450);
    } TEST_END();

    TEST_CASE("AI17.20 A Crew One Ant Short Gets The Bomber For The Last Token: When Duds And Blows Leave One Ant Fewer With The Hit Points Than The Tokens Need, The Bomber Flies On And Takes The Last Swimmer (A Hard Bot, Seat 0, Seed 4, Latency 3: Three Swimmers And No Attempt Given Up, Without It Two And One Given Up; A Medium Bot, Seat 0, Seed 2: Three Swimmers, Without It Two)")
    {
        const Run a = play(Level::Hard, 0, 4, 2400, {}, 3);
        ASSERT_EQ(a.swimmers, 3u);
        ASSERT_EQ(a.given_up, 0u);
        const Run b = play(Level::Medium, 0, 2, 3200);
        ASSERT_EQ(b.swimmers, 3u);
        ASSERT_EQ(b.given_up, 0u);
    } TEST_END();

    TEST_CASE("AI17.21 The Bomber Is Flown Only Where It Is Needed: Five Ants Over Two Flights Make Ten Hops And Ten Bombs (A Medium Bot, Seat 0, Seed 9, No Dud); On Seat 3 The Bomber Flies On Once, Over The First Flight, For The Island Of The Second Has None To Take: Five Ants And The Bomber Make Eleven Hops (A Hard Bot, Latency 3, Seed 12, No Dud)")
    {
        const Run a = play(Level::Medium, 0, 9, 2400);
        ASSERT_EQ(a.duds, 0u);
        ASSERT_EQ(a.hops, 10u);
        ASSERT_EQ(a.planted, 10u);
        ASSERT_EQ(a.swimmers, 3u);
        const Run b = play(Level::Hard, 3, 12, 2400, {}, 3);
        ASSERT_EQ(b.duds, 0u);
        ASSERT_EQ(b.hops, 11u);
        ASSERT_EQ(b.swimmers, 3u);
    } TEST_END();

    TEST_CASE("AI17.22 An Ant That Cannot Hop Is Sent Off The Shore Tile: An Ant Left With 2 Hit Points While It Waits There (A Medium Bot, Seats 0 And 3, Seed 9) Is Not Sent Onto A Bomb, Which Would Kill It, And Does Not Hold The Tile Against The Others: The Four That Are Left And The Bomber Take Five Tokens And Three Swimmers, And No Attempt Is Given Up")
    {
        for (const uint8_t seat : {uint8_t{0}, uint8_t{3}}) {
            const Stranded s = strand(Level::Medium, seat, 9);
            ASSERT_TRUE(s.ant != 0 && s.tick < 200);
            ASSERT_TRUE(s.alive);
            ASSERT_EQ(s.taken, 5u);
            ASSERT_EQ(s.swimmers, 3u);
            ASSERT_EQ(s.given_up, 0u);
        }
    } TEST_END();

    TEST_CASE("AI17.23 The Bomber Takes The Last Token That Is Needed And No Other: With One Ant Short (A Medium Bot, Seat 0, Seed 4) The Four Plain Ants Take Fire, Fire, Swimmer, Swimmer And The Bomber The Last Swimmer; Taking The Fourth Token It Would Have Left Its Own Power-Up Behind It In The Row (One Tile Wide), Shut The Row And Held The Expedition Up At Two Swimmers; With Two Short (Seed 7) The Bomber Stays At Home, For It Could Not Help")
    {
        const Stranded a = strand(Level::Medium, 0, 4);
        ASSERT_TRUE(a.ant != 0);
        ASSERT_EQ(a.taken, 5u);
        ASSERT_EQ(a.swimmers, 3u);
        ASSERT_EQ(a.given_up, 0u);
        const Stranded b = strand(Level::Medium, 0, 7);
        ASSERT_TRUE(b.ant != 0);
        ASSERT_EQ(b.taken, 3u);
        ASSERT_EQ(b.swimmers, 1u);
        ASSERT_EQ(b.bombers_on_row, 0);
    } TEST_END();

    TEST_CASE("AI17.24 A Hill Island With No Bomber To Take Plans Nothing: With The Bomber Power-Up Of The Hill's Island Gone (Medium And Hard, Seat 0, Seed 1) No Route Starts, So No Crew Is Claimed And Nothing Is Given Up; Counted From An Island Without A Bomber, The Route Would Claim The Crew At Once And Wait For A Bomber That Never Comes")
    {
        for (const Level level : {Level::Medium, Level::Hard}) {
            Match m;
            m.expedition = true;
            m.ferry = true;
            m.init("ISLANDS", 1, 1u, level, 0, {}, [](sim::SimulationEngine& engine) {
                const MapInfo probe(engine);
                for (const PowerUpInfo& p : probe.powerups()) {
                    if (p.type == sim::AntType::Bomber && p.approach[0].reachable()) engine.grid_mut().clear_powerup(p.tile.x, p.tile.y);
                }
            });
            m.run(1500);
            const ExpeditionTask& ex = m.bot(0)->expedition();
            ASSERT_EQ(ex.planned(), 0u);
            ASSERT_EQ(ex.crew_size(), 0u);
            ASSERT_EQ(ex.given_up(), 0u);
        }
    } TEST_END();

    TEST_CASE("AI17.25 A Leg Whose Bomber Has Flown On Makes No New One: When A Second Bomber Power-Up Lies On The Hill's Island After The Bomber Of Seat 3 Has Flown On (A Medium Bot, Seed 1), Nobody Goes For It, And The Expedition Still Has Its Three Swimmers; A Leg That Did Not Know That Its Bomber Was Gone Would Send A Worker To Take It")
    {
        static constexpr uint8_t seat = 3;
        Match m;
        m.expedition = true;
        m.ferry = true;
        sim::TileCoord spare{-1, -1};
        m.init("ISLANDS", 1, static_cast<uint8_t>(1u << seat), Level::Medium, 0, {}, [&spare](sim::SimulationEngine& engine) {
            const MapInfo probe(engine);                                                             // the free tiles of the hill's island near its Bomber power-up: a second power-up on the first (the analysis takes it in), a worker each on the next two
            std::vector<sim::TileCoord> free_tiles;
            for (const PowerUpInfo& p : probe.powerups()) {
                if (p.type != sim::AntType::Bomber || !p.approach[seat].reachable()) continue;
                for (int dy = -4; dy <= 4; ++dy) {
                    for (int dx = -4; dx <= 4; ++dx) {
                        const sim::TileCoord t{p.tile.x + dx, p.tile.y + dy};
                        if (!engine.grid().in_bounds(t) || probe.component(seat, t) != probe.hill_component(seat) || engine.grid().has_powerup_at(t)) continue;
                        bool taken = false;
                        for (const auto& a : engine.get_world_state().ants) taken = taken || (a.tile_x == t.x && a.tile_y == t.y);
                        if (!taken) free_tiles.push_back(t);
                    }
                }
            }
            if (free_tiles.size() < 3) return;                                                       // (spare stays unset, and the assertion after the call fails)
            spare = free_tiles[0];
            engine.grid_mut().place_powerup(spare.x, spare.y, 1);
            engine.spawn_unit(seat, sim::AntType::Worker, free_tiles[1]);                            // (two workers more at home: the crew takes what it needs, and one ant is left to go for a power-up)
            engine.spawn_unit(seat, sim::AntType::Worker, free_tiles[2]);
        });
        ASSERT_TRUE(spare.x >= 0);
        m.sim.grid_mut().clear_powerup(spare.x, spare.y);                                            // (taken away again: the match starts as it does without it)
        const ExpeditionTask& ex = m.bot(seat)->expedition();
        uint32_t bomber = 0;
        int flown = 0;
        for (int t = 1; t <= 3000 && flown == 0; ++t) {
            m.tick();
            if (ex.bomber_of(0) != 0) bomber = ex.bomber_of(0);
            else if (bomber != 0 && ex.active()) flown = t;                                          // the first leg has lost its Bomber while the expedition goes on: it flew on
        }
        ASSERT_TRUE(flown > 0);
        bool seen = false;
        for (const auto& a : m.sim.get_world_state().ants) {
            if (a.id != bomber) continue;
            seen = true;
            ASSERT_TRUE(m.ctl->map().ant_component(seat, sim::TileCoord{a.tile_x, a.tile_y}) != m.ctl->map().hill_component(seat));
        }
        ASSERT_TRUE(seen);
        m.sim.grid_mut().place_powerup(spare.x, spare.y, 1);
        m.run(600);
        ASSERT_TRUE(ex.active());
        ASSERT_TRUE(m.sim.grid().has_powerup_at(spare));                                             // (a leg that took it would have sent a worker long before)
        m.run(4200 - flown - 600);
        ASSERT_EQ(ex.swimmers_taken(), 3u);
    } TEST_END();

    TEST_CASE("AI17.26 The Bomber Of An Earlier Flight Does Not Follow One That Has Flown On: A Hard Bot On Seat 0 (Seed 3) Flies Over Two Islands, The Crew Is One Ant Short And The Bomber Of The Second Flight Takes The Last Token; The Bomber Of The First Stays At Home (14 Bombs, 14 Hops), Where It Once Followed To An Island That Nobody Needed It On (15 And 15) And Was Lost To The Economy")
    {
        Match m;
        m.expedition = true;
        m.ferry = true;
        m.init("ISLANDS", 3, 1u, Level::Hard, 0);
        const ExpeditionTask& ex = m.bot(0)->expedition();
        uint32_t first = 0;                                                                          // the Bomber of the first flight, and of the second
        uint32_t second = 0;
        int over = 0;                                                                                // the tick at which the second flight lost its Bomber: it flew on, to take the last token
        int ended = 0;                                                                               // the tick at which the expedition was over
        for (int t = 1; t <= 3200 && ended == 0; ++t) {
            m.tick();
            if (ex.bomber_of(0) != 0) first = ex.bomber_of(0);
            if (ex.bomber_of(1) != 0) second = ex.bomber_of(1);
            else if (second != 0 && over == 0 && ex.active()) over = t;
            if (over != 0 && t == over + 50) {
                ASSERT_TRUE(first != 0 && second != 0 && first != second);
                ASSERT_EQ(ex.bomber_of(0), first);                                                   // (the first flight has its Bomber still, the second none: it flew on)
                ASSERT_EQ(ex.bomber_of(1), 0u);
            }
            if (ex.planned() > 0 && !ex.active()) ended = t;
        }
        ASSERT_TRUE(over > 0 && ended > over);
        ASSERT_EQ(ex.swimmers_taken(), 3u);
        ASSERT_EQ(ex.hops(), 14u);
        ASSERT_EQ(ex.planted(), 14u);
        bool seen = false;
        for (const auto& a : m.sim.get_world_state().ants) {
            if (a.id != first) continue;
            seen = true;
            ASSERT_EQ(m.ctl->map().ant_component(0, sim::TileCoord{a.tile_x, a.tile_y}), m.ctl->map().hill_component(0));
        }
        ASSERT_TRUE(seen);
    } TEST_END();

    TEST_CASE("AI17.27 An Ant That Cannot Hop Any More Still Counts For A Token Once It Stands On The Island Of The Row, For It Walks: A Hard Bot On Seat 0 (Seed 22) Has An Ant Left With 2 Hit Points By The Last Flight, And The Crew Of Two Is Not One Ant Short Of Two Tokens (12 Hops, 13 Bombs, Three Swimmers); Counted Out, Both Bombers Would Fly On For Nothing (14 And 14)")
    {
        const Run r = play(Level::Hard, 0, 22, 3200);
        ASSERT_EQ(r.swimmers, 3u);
        ASSERT_EQ(r.given_up, 0u);
        ASSERT_EQ(r.hops, 12u);
        ASSERT_EQ(r.planted, 13u);
    } TEST_END();

    TEST_CASE("AI17.28 One Bomber Flies On For A Crew That Is One Ant Short, Not Two: Once The Bomber Of One Flight Is In The Row With The Last Token To Take (A Medium Bot On Seat 1, Seed 19, Latency 3) The Bomber Of The Other Stays Where It Is (17 Hops And 20 Bombs, Three Swimmers); Counting A Bomber That Has Flown On For An Ant Of The Crew, Which Is One Ant Short Then, Flies The Other One On For Nothing (18 And 21)")
    {
        const Run r = play(Level::Medium, 1, 19, 3600, {}, 3);
        ASSERT_EQ(r.swimmers, 3u);
        ASSERT_EQ(r.hops, 17u);
        ASSERT_EQ(r.planted, 20u);
    } TEST_END();

    TEST_CASE("AI17.29 A Dud Is Told By The Ant That Burns Or Lies Stunned On B, And By Nothing Else: In Six Matches With The Arena's Latency Of 3 Ticks (Easy On Seat 3, Seed 8, And On Seat 0, Seed 9; Medium On Seat 0, Seed 4, And On Seat 1, Seed 3; Easy On Seat 3, Seed 7, And On Seat 0, Seed 5) The Duds That The Task Counts Are The Times That An Ant Of The Seat Began To Burn (3, 0, 3, 3, 4 And 5); A Clock That Calls Every Ant That Is Still On B After 60 Ticks The Victim Of A Dud Counts The Ants That A Blast Had Thrown And That Lie Stunned There (8, 5, 5 And 5 In The First Four), And A Reading Of The Burning State Alone Misses The Ants That The Look Of An Easy Bot Finds Stunned By Then (1 And 3 In The Last Two)")
    {
        struct Case {
            Level level;
            uint8_t seat;
            uint32_t seed;
            uint32_t duds;
        };
        for (const Case& k : {Case{Level::Easy, 3, 8, 3}, Case{Level::Easy, 0, 9, 0}, Case{Level::Medium, 0, 4, 3}, Case{Level::Medium, 1, 3, 3}, Case{Level::Easy, 3, 7, 4}, Case{Level::Easy, 0, 5, 5}}) {
            const Run r = play(k.level, k.seat, k.seed, 3600, {}, 3);
            ASSERT_EQ(r.burns, k.duds);
            ASSERT_EQ(r.duds, k.duds);
        }
    } TEST_END();

    TEST_CASE("AI17.30 A Bomber That Has Flown On And Cannot Hop Again Does Not Count For The Last Token Unless It Stands On The Island Of The Row: When The Bomber Of The First Flight Is Left With 2 Hit Points Where It Landed, After An Ant Of The Crew Was (A Medium Bot, Seat 0, Seed 1; A Hard Bot, Seat 1, Seed 1, Latency 3), The Bomber Of The Second Flight Flies On For The Last Token: Five Tokens And Three Swimmers, No Attempt Given Up (With The Stranded Bomber Counted: Four Tokens, Two Swimmers, And The Attempt Given Up)")
    {
        const Stranded a = strand(Level::Medium, 0, 1, 0, true);
        ASSERT_TRUE(a.ant != 0 && a.bomber != 0);
        ASSERT_EQ(a.taken, 5u);
        ASSERT_EQ(a.swimmers, 3u);
        ASSERT_EQ(a.given_up, 0u);
        const Stranded b = strand(Level::Hard, 1, 1, 3, true);
        ASSERT_TRUE(b.ant != 0 && b.bomber != 0);
        ASSERT_EQ(b.taken, 5u);
        ASSERT_EQ(b.swimmers, 3u);
        ASSERT_EQ(b.given_up, 0u);
    } TEST_END();

    TEST_CASE("AI17.31 A Crew That Can No Longer Take A Token Is Given Up After 200 Ticks, Not 2,400: When The Hit Points That A Hop Needs Are Raised To 9 After The First Landing (Every Ant Has 8 After A Hop, And Nobody Can Hop Again; A Medium Bot, Seat 0, Seed 1), The Attempt Is Given Up Within 300 Ticks Of The Last Thing That Moved It On, And The Crew Is Free; Without The Rule It Holds The Ants For 2,400")
    {
        Match m;
        m.expedition = true;
        m.ferry = true;
        m.init("ISLANDS", 1, 1u, Level::Medium, 0);
        const ExpeditionTask& ex = m.bot(0)->expedition();
        ExpeditionTask::Params p = ex.params();
        p.min_hp = 9;
        bool raised = false;
        uint32_t moved = 0;                                                                          // planted + hops + landings + tokens: it grows with each thing that moves the attempt on
        uint64_t last_move = 0;
        uint64_t gave_up = 0;
        for (uint64_t t = 1; t <= 2600; ++t) {
            m.tick();
            if (!raised && ex.landings() >= 1) {
                m.bot(0)->expedition_for_labs().set_params(p);
                raised = true;
            }
            const uint32_t now_moved = ex.planted() + ex.hops() + ex.landings() + ex.taken();
            if (now_moved != moved) {
                moved = now_moved;
                last_move = t;
            }
            if (gave_up == 0 && ex.given_up() > 0) gave_up = t;
        }
        ASSERT_TRUE(raised);
        ASSERT_EQ(ex.planned(), 1u);
        ASSERT_EQ(ex.given_up(), 1u);
        ASSERT_TRUE(gave_up > last_move && gave_up - last_move <= 300);
        ASSERT_EQ(ex.crew_size(), 0u);
    } TEST_END();

    TEST_CASE("AI17.32 A Bomber Is The Bomber Of One Leg At A Time: On Seat 3 (A Medium Bot, Seed 1) The Bomber Of The Hill Flies On With The Crew To The Satellite That Has None, The Leg From There Takes It Over, And The First Leg Names It No More (A First Leg That Kept Naming It Would Have Two Legs Drive One Ant Until The Expedition Is Over)")
    {
        Match m;
        m.expedition = true;
        m.ferry = true;
        m.init("ISLANDS", 1, 8u, Level::Medium, 0);
        const ExpeditionTask& ex = m.bot(3)->expedition();
        uint32_t first = 0;                                                                          // the Bomber of the first leg, until it flew on
        bool taken_over = false;
        for (int t = 1; t <= 3600; ++t) {
            m.tick();
            if (ex.bomber_of(0) != 0) first = ex.bomber_of(0);
            taken_over = taken_over || (first != 0 && ex.bomber_of(1) == first);
            ASSERT_TRUE(ex.bomber_of(0) == 0 || ex.bomber_of(0) != ex.bomber_of(1));
        }
        ASSERT_TRUE(taken_over);
    } TEST_END();

    TEST_CASE("AI17.33 A Bot As The Arena Plays It Hands Its Plan To The Expedition At The Start: With A Style Drawn And The Level's Plan Tuned, The Bomber Flies On At Medium And Hard And Not At Easy, Or The Other Way Round When The Tuning Turns It (`ifly`); On Seat 3 The Route Is Two Flights With It And The Old Three Without")
    {
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            for (const bool turned : {false, true}) {
                Match m;
                m.styled = true;
                m.init("ISLANDS", 1, 8u, level, 0, turned ? std::function<void(LevelPlan&)>([](LevelPlan& p) { p.island_fly_on = !p.island_fly_on; }) : std::function<void(LevelPlan&)>{});
                const bool fly_on = (level != Level::Easy) != turned;
                const ExpeditionTask& ex = m.bot(3)->expedition();
                ASSERT_EQ(ex.params().fly_on, fly_on);
                m.run(150);
                ASSERT_EQ(ex.planned(), 1u);
                ASSERT_EQ(ex.route().size(), fly_on ? 3u : 4u);
            }
        }
    } TEST_END();

    TEST_CASE("AI17.34 A Bomber With Too Few Hit Points Does Not Join The Crew To Fly On: When The Bomber Of The First Flight On Seat 3 (A Medium Bot, Seed 1) Is Left With 2 Hit Points Before The Crew Is Over, It Stays At Home And Alive, The Crew Stays The Five Ants It Was, And Waits On The Satellite For A Bomber That Is Not There Until The Attempt Is Given Up")
    {
        Match m;
        m.expedition = true;
        m.ferry = true;
        m.init("ISLANDS", 1, 8u, Level::Medium, 0);
        const ExpeditionTask& ex = m.bot(3)->expedition();
        uint32_t bomber = 0;
        size_t crew = 0;                                                                             // the crew at the first look: it only shrinks, unless the Bomber is made one of it
        for (int t = 1; t <= 3600; ++t) {
            m.tick();
            if (crew == 0) crew = ex.crew_size();
            ASSERT_TRUE(ex.crew_size() <= crew);
            if (bomber != 0 || ex.bomber_of(0) == 0) continue;
            for (const auto& a : m.sim.get_world_state().ants) {
                if (a.id != ex.bomber_of(0) || a.raw_type != sim::AntType::Bomber) continue;
                bomber = a.id;
                m.sim.get_unit(bomber).hp = 2;                                                       // (a blow, or a dud, would have done it)
            }
        }
        ASSERT_TRUE(bomber != 0 && crew > 0);
        ASSERT_EQ(ex.given_up(), 1u);
        bool seen = false;
        for (const auto& a : m.sim.get_world_state().ants) {
            if (a.id != bomber) continue;
            seen = true;
            ASSERT_TRUE(a.hp > 0);
            ASSERT_EQ(m.ctl->map().ant_component(3, sim::TileCoord{a.tile_x, a.tile_y}), m.ctl->map().hill_component(3));
        }
        ASSERT_TRUE(seen);
    } TEST_END();

    TEST_CASE("AI17.35 Where No Bomber Flies On, A Crew That Cannot Take The Last Token Is Given Up After 200 Ticks Too: At Easy (Seat 0, Seed 1) The Two Ants That Are Left On The Middle Island, Left With 2 Hit Points When The Third Token Was Taken, Cannot Take The Fourth, And The Attempt Is Given Up Within 400 Ticks (Counting On A Bomber For The Last Token, As At Medium, The Ants Would Be Held For 2,400)")
    {
        Match m;
        m.expedition = true;
        m.ferry = true;
        m.init("ISLANDS", 1, 1u, Level::Easy, 0);
        const ExpeditionTask& ex = m.bot(0)->expedition();
        uint64_t hurt_at = 0;
        uint64_t gave_up = 0;
        for (uint64_t t = 1; t <= 9000 && (hurt_at == 0 || t <= hurt_at + 600); ++t) {
            m.tick();
            if (hurt_at == 0 && ex.taken() == 3) {
                for (const auto& a : m.sim.get_world_state().ants) {
                    if (a.player_id == 0 && a.raw_type == sim::AntType::Worker && m.ctl->map().ant_component(0, sim::TileCoord{a.tile_x, a.tile_y}) != ex.route().back()) m.sim.get_unit(a.id).hp = 2;
                }
                hurt_at = t;
            }
            if (gave_up == 0 && ex.given_up() > 0) gave_up = t;
        }
        ASSERT_TRUE(hurt_at > 0);
        ASSERT_EQ(ex.swimmers_taken(), 1u);
        ASSERT_EQ(ex.given_up(), 1u);
        ASSERT_TRUE(gave_up > hurt_at && gave_up - hurt_at <= 400);
        ASSERT_EQ(ex.crew_size(), 0u);
    } TEST_END();

    TEST_CASE("AI17.36 A Crew With One Token Left Is Not Given Up While A Bomber Can Still Fly On For It: In A Match Of Four Medium Bots (Seed 67, Seat 2) The Ants Of The Crew That Are Left Cannot Hop, A Bomber Goes On After 7 Duds In 18 Bombs, And The Third Swimmer Is Taken At Tick 3,463, 1,500 Ticks After The Fourth Token, With No Attempt Given Up (The Attempt Is Given Up At Tick 2,183, 220 Ticks After It Last Moved, If One Token Left Counts As Hopeless Too)")
    {
        const Natural r = four_bots(Level::Medium, 67, 3, 2, 4200);
        ASSERT_EQ(r.given_up, 0u);
        ASSERT_EQ(r.swimmers, 3u);
        ASSERT_TRUE(r.third > 0 && r.third < 4000);
        ASSERT_TRUE(r.wait > 200);                                                                   // (the last token is waited for, 1,500 ticks, and not given up after the 200 of a hopeless crew)
    } TEST_END();

    TEST_CASE("AI17.37 A Bomber Does Not Hold A Crew That Needs Two Tokens: In A Match Of Four Hard Bots (Seed 75, Seat 0, Commands At Once) The Only Ant Of The Crew That Can Take A Token, A Bomber On The Island Of The Row, Takes The Last Token And No Other, The Two Plain Ants Left On The Middle Island Have 2 Hit Points And Two Tokens Are Left: The Attempt Is Given Up At Tick 1,701, 204 Ticks After It Last Moved (If That Bomber Counts As An Ant That Takes Any Token, At Tick 3,901)")
    {
        const Natural r = four_bots(Level::Hard, 75, 0, 0, 2400);
        ASSERT_TRUE(r.gave_up > 0 && r.gave_up <= 1800);
        ASSERT_EQ(r.swimmers, 1u);
        ASSERT_TRUE(r.gave_up - r.last_take >= 200 && r.gave_up - r.last_take <= 260);               // (the 200 ticks of a hopeless crew after the last token, not the 2,400 of one that is only slow)
    } TEST_END();

    TEST_CASE("AI17.38 A Plain Ant Takes The Last Token Before A Bomber That Stands Nearer To The Entrance: In A Match Of Four Hard Bots (Seed 120, Seat 2) A Worker And A Bomber That Has Flown On Wait On The Island Of The Row For The Last Token, The Bomber Nearer, And The Worker Takes It: The Third Swimmer Is Taken At Tick 1,291 And The Bomber Is A Bomber Still (It Would Be A Swimmer With The Bomber Taking It)")
    {
        const Natural r = four_bots(Level::Hard, 120, 3, 2, 3000);
        ASSERT_EQ(r.given_up, 0u);
        ASSERT_TRUE(r.third > 0 && r.third < 2000);
        ASSERT_EQ(r.bombers_on_row, 1);
    } TEST_END();

    TEST_CASE("AI17.39 A Crew Ant That Stands On The Island Of The Row Takes A Token With Any Hit Points, And A Crew Of Such Ants Is Not Given Up: At Easy (Seat 0, Seed 1) Every Plain Ant That Lands On That Island Is Left With 2 Hit Points (Five Ants), And Four Tokens Are Taken, Two Of Them Swimmers, With No Attempt Given Up (If Only An Ant With The Hit Points To Hop Counted, The Crew Is Given Up At Tick 4,801 With Three Tokens And One Swimmer)")
    {
        Match m;
        m.expedition = true;
        m.ferry = true;
        m.init("ISLANDS", 1, 1u, Level::Easy, 0);
        const ExpeditionTask& ex = m.bot(0)->expedition();
        for (int t = 1; t <= 6000; ++t) {
            m.tick();
            if (ex.route().size() < 2) continue;
            for (const auto& a : m.sim.get_world_state().ants) {
                if (a.player_id == 0 && a.raw_type == sim::AntType::Worker && a.hp >= 4 && m.ctl->map().ant_component(0, sim::TileCoord{a.tile_x, a.tile_y}) == ex.route().back()) m.sim.get_unit(a.id).hp = 2;
            }
        }
        ASSERT_EQ(ex.taken(), 4u);
        ASSERT_EQ(ex.swimmers_taken(), 2u);
        ASSERT_EQ(ex.given_up(), 0u);
    } TEST_END();
}
