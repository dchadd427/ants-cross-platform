// AI15: the island task of the standard bot (island_tasks.hpp, docs/BOTS.md "Islands"): a Swimmer digs the bridges, the economy walks over them, one ant to a bridge, and the ants are kept off a
// bridge that is about to go. The quick cases are here (a few thousand ticks of one seat); the whole matches of the four seats are suite 2.28 (test_ai_islands, AI16).
#include <functional>

#include "ai_test.hpp"
#include "island_fixture.hpp"

namespace {

using namespace island_test;
using namespace ants;
using namespace ants::ai;

bool on_bridge(const sim::SimulationEngine& sim, const sim::AntSnapshot& a) { return sim.grid().get_cell(TileCoord{a.tile_x, a.tile_y}).has_completed_bridge(); }

// A tile of the engine's grid that is a finished bridge now
bool finished(const sim::SimulationEngine& sim, TileCoord t) { return sim.grid().get_cell(t).has_completed_bridge(); }

void tick_engine(sim::SimulationEngine& sim) {
    sim.tick();
    sim.clear_news_events();
    sim.clear_audio_events();
}

// The north chain of ISLANDS: three water tiles, (24,18), (24,17), (24,16), between the north shore of the hill's island and the south shore of the island of the north (the pile beyond it is at
// (26,12)), dug by a Swimmer of team 0 that the test spawns and sends off the bridge again (a Swimmer that stands on a one-wide bridge blocks it; the builder of the bot steps off as well).
// `tick` advances the world (the bots, when there are any). Returns the tick at which the middle tile was finished.
uint64_t dig_north_chain(sim::SimulationEngine& sim, const std::function<void()>& tick) {
    const uint32_t swimmer = sim.spawn_unit(0, sim::AntType::Swimmer, TileCoord{24, 19});
    uint64_t middle = 0;
    for (const TileCoord t : {TileCoord{24, 18}, TileCoord{24, 17}, TileCoord{24, 16}}) {
        Command c;
        c.type = CommandType::GroupSpecial;
        c.issuer = 0;
        c.tile_x = static_cast<int16_t>(t.x);
        c.tile_y = static_cast<int16_t>(t.y);
        c.ants = {swimmer};
        sim.apply_command(c);
        for (int i = 0; i < 120 && !finished(sim, t); ++i) tick();
        if (t.y == 17) middle = sim.current_tick();
    }
    Command off;
    off.type = CommandType::GroupMove;
    off.issuer = 0;
    off.tile_x = 23;
    off.tile_y = 17;
    off.ants = {swimmer};
    sim.apply_command(off);
    for (int i = 0; i < 200; ++i) {
        tick();
        bool on = false;
        for (const auto& a : sim.get_world_state().ants) on = on || (a.id == swimmer && on_bridge(sim, a));
        if (!on && i > 20) break;
    }
    return middle;
}

}  // namespace

void run_island_task_tests() {
    TEST_CASE("AI15.1 The Island Task Is Idle On The Maps With No Water That Separates Anything: No Claim, No Order, No Want, No Limit, No Closed Pile, On TINY, MEDIUM, GAUNTLET And TREASURE At Every Level")
    {
        for (const char* map : {"TINY", "MEDIUM", "GAUNTLET", "TREASURE"}) {
            for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
                Match m;
                m.init(map, 3, 0x01, level, 0);
                m.run(1500);
                const IslandTask& is = m.island(0);
                ASSERT_FALSE(is.active());
                ASSERT_EQ(is.tiles_ordered(), 0u);
                ASSERT_EQ(is.guarded(), 0u);
                ASSERT_EQ(is.recalls() + is.escapes() + is.holds() + is.repaths(), 0u);
                ASSERT_TRUE(is.pile_limits().empty());
                ASSERT_TRUE(is.closed_piles().empty());
                ASSERT_TRUE(m.bot(0)->tactics().shut_tiles.empty());
                ASSERT_TRUE(is.chain().empty() && is.planned().empty());
            }
        }
    } TEST_END();

    TEST_CASE("AI15.2 On ISLANDS And SMALL The Island Task Is On From The First Look; The Level Plans Switch It On; A Plan Made By Hand Has It Off")
    {
        for (const char* map : {"ISLANDS", "SMALL"}) {
            Match m;
            m.init(map, 1, 0x01, Level::Medium, 0x01);
            m.run(60);
            ASSERT_TRUE(m.island(0).active());
        }
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) ASSERT_TRUE(plan_for(level).islands);
        ASSERT_FALSE(LevelPlan{}.islands);
        Match off;
        off.init("ISLANDS", 1, 0x01, Level::Medium, 0x01, [](LevelPlan& p) { p.islands = false; });
        off.run(600);
        ASSERT_FALSE(off.island(0).active());
        ASSERT_EQ(off.island(0).tiles_ordered(), 0u);
    } TEST_END();

    TEST_CASE("AI15.3 A Swimmer Digs A Chain Across The Channel That The Planner Chose And A Worker Crosses It: The First Chain Is Finished Within 700 Ticks (A Medium Bot, ISLANDS, Seat 0, A Swimmer At Its Gate), Every Tile Of It Is A Finished Bridge, And Within 2,500 Ticks A Worker Stands On An Island That No Walker Reaches From The Hill")
    {
        Match m;
        m.init("ISLANDS", 1, 0x01, Level::Medium, 0x01);
        const MapInfo& map = m.ctl->map();
        std::vector<TileCoord> first;
        uint64_t done_at = 0;
        bool crossed = false;
        for (int t = 1; t <= 2500; ++t) {
            m.tick();
            const IslandTask& is = m.island(0);
            if (first.empty() && !is.chain().empty()) first = is.chain();
            if (done_at == 0 && is.chains_finished() >= 1) done_at = static_cast<uint64_t>(t);
            for (const auto& a : m.sim.get_world_state().ants) {
                if (a.player_id != 0 || a.type != sim::AntType::Worker) continue;
                const int32_t comp = map.component(0, TileCoord{a.tile_x, a.tile_y});
                if (comp >= 0 && comp != map.hill_component(0)) crossed = true;
            }
        }
        ASSERT_TRUE(!first.empty() && first.size() <= 6);
        ASSERT_TRUE(done_at > 0 && done_at <= 700);
        for (const TileCoord t : first) ASSERT_TRUE(finished(m.sim, t));                                  // (3,600 ticks of life: the first chain still stands)
        ASSERT_TRUE(crossed);
    } TEST_END();

    TEST_CASE("AI15.4 A Worker Carries Food Home Across A Bridge: In 3,000 Ticks The Seat Scores, And At Some Tick An Ant That Holds Food Stands On A Finished Bridge Tile (A Medium Bot, ISLANDS, Seat 0)")
    {
        Match m;
        m.init("ISLANDS", 1, 0x01, Level::Medium, 0x01);
        bool carried = false;
        uint64_t first_score = 0;
        for (int t = 1; t <= 3000; ++t) {
            m.tick();
            if (first_score == 0 && m.sim.get_player_score(0) > 0) first_score = static_cast<uint64_t>(t);
            for (const auto& a : m.sim.get_world_state().ants) {
                if (a.player_id == 0 && a.is_holding && on_bridge(m.sim, a)) carried = true;
            }
        }
        ASSERT_TRUE(carried);
        ASSERT_TRUE(first_score > 0 && first_score <= 2000);
        ASSERT_TRUE(m.sim.get_player_score(0) >= 120);
        ASSERT_EQ(m.total_collapsed(), 0);
    } TEST_END();

    TEST_CASE("AI15.5 Two Ants That Meet Head-On On A One-Wide Bridge Stop For Good (The Engine's Own Rule, And The Reason The Economy Sends One Ant At A Time Over A Bridge): One Walks North And One South Over The Three-Tile Chain, And After 900 Ticks Neither Has Got Across And Neither Has Moved For 600")
    {
        sim::SimulationEngine sim;
        start_match(sim, "ISLANDS", 1, 0x01);
        dig_north_chain(sim, [&] { tick_engine(sim); });
        const uint32_t north = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{24, 19});
        const uint32_t south = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{24, 15});
        const auto go = [&](uint32_t ant, TileCoord to) {
            Command c;
            c.type = CommandType::GroupMove;
            c.issuer = 0;
            c.tile_x = static_cast<int16_t>(to.x);
            c.tile_y = static_cast<int16_t>(to.y);
            c.ants = {ant};
            sim.apply_command(c);
        };
        const auto where = [&](uint32_t ant) {
            for (const auto& a : sim.get_world_state().ants) {
                if (a.id == ant) return TileCoord{a.tile_x, a.tile_y};
            }
            return TileCoord{-1, -1};
        };
        go(north, TileCoord{24, 14});
        go(south, TileCoord{24, 20});
        for (int i = 0; i < 300; ++i) tick_engine(sim);
        const TileCoord n300 = where(north), s300 = where(south);
        for (int i = 0; i < 600; ++i) tick_engine(sim);
        ASSERT_TRUE(where(north) == n300 && where(south) == s300);                                       // (they stand where they stood 600 ticks ago)
        ASSERT_TRUE(where(north).y >= 16 && where(north).y <= 19 && where(south).y >= 15 && where(south).y <= 18);   // (on the chain or at its ends: not across)
        ASSERT_TRUE(where(north) != (TileCoord{24, 14}) && where(south) != (TileCoord{24, 20}));
    } TEST_END();

    TEST_CASE("AI15.6 One Ant To A Bridge: At Most One Ant Works A Pile Of The Limits At Every Tick Of 5,000 (A Medium Bot, ISLANDS, Seat 0, A Swimmer At Its Gate), No Ant Stands Idle On A Bridge For 300 Ticks, And With Eight Ants To A Bridge The Count Does Go Above One")
    {
        const auto trial = [](uint32_t bridge_ants, uint32_t* most, int* longest) {
            Match m;
            m.init("ISLANDS", 1, 0x01, Level::Medium, 0x01, [&](LevelPlan& p) { p.island_bridge_ants = bridge_ants; });
            std::map<uint32_t, int> idle_on_bridge;
            *longest = 0;
            *most = 0;
            for (int t = 1; t <= 5000; ++t) {
                m.tick();
                std::map<uint32_t, uint32_t> works;
                for (const auto& a : m.sim.get_world_state().ants) {
                    if (a.player_id != 0 || a.type == sim::AntType::Swimmer) continue;
                    uint32_t pile = 0;
                    if (m.bot(0)->harvest().assigned_pile(a.id, pile) && m.island(0).pile_limits().count(pile) != 0) *most = std::max(*most, ++works[pile]);
                    const bool still = a.state == sim::UnitState::Idle || a.state == sim::UnitState::CantGo;
                    if (still && on_bridge(m.sim, a)) *longest = std::max(*longest, ++idle_on_bridge[a.id]);
                    else idle_on_bridge[a.id] = 0;
                }
            }
            ASSERT_EQ(m.total_collapsed(), 0);
        };
        uint32_t most_one = 0, most_eight = 0;
        int longest_one = 0, longest_eight = 0;
        trial(1, &most_one, &longest_one);
        ASSERT_TRUE(most_one <= 1);
        ASSERT_TRUE(longest_one < 300);
        trial(8, &most_eight, &longest_eight);
        ASSERT_TRUE(most_eight > 1);                                                                         // (the count can see a second ant: the limit is what keeps it at one)
    } TEST_END();

    // The bridge of the test (the north chain) under a bot that has no builder of its own: the economy works the pile of 300 points beyond it until the bridge goes, 3,600 ticks after it was finished.
    // An ant that is on the north island when the bridge goes is cut off from the hill for the rest of the match
    const auto lone = [](LevelPlan& p) { p.island_builders = 0; };
    const auto cut_off = [](Match& m) {
        const MapInfo& map = m.ctl->map();
        const int32_t north = map.component(0, TileCoord{25, 14});
        int n = 0;
        for (const auto& a : m.sim.get_world_state().ants) n += (a.player_id == 0 && map.ant_component(0, TileCoord{a.tile_x, a.tile_y}) == north) ? 1 : 0;
        return n;
    };

    TEST_CASE("AI15.7 No Ant Is Lost When A Bridge Collapses Under The Economy: A Pile Over A Dug Chain Is Worked Until The Chain Goes (At About Tick 3,750), And Not One Ant Drowns Or Stays Behind On The Far Island (A Medium Bot, ISLANDS, Seat 0); The Same Match With The Guard Off Leaves An Ant Behind")
    {
        Match guarded;
        guarded.init("ISLANDS", 1, 0x01, Level::Medium, 0, lone);
        dig_north_chain(guarded.sim, [&] { guarded.tick(); });
        guarded.run(4300);
        ASSERT_EQ(guarded.total_collapsed(), 0);
        ASSERT_EQ(cut_off(guarded), 0);
        ASSERT_TRUE(guarded.island(0).holds() + guarded.island(0).recalls() + guarded.island(0).escapes() > 0);           // (the guard did something)
        ASSERT_TRUE(guarded.sim.get_player_score(0) >= 150);                                                               // (the economy worked over the chain)
        ASSERT_FALSE(finished(guarded.sim, TileCoord{24, 18}));                                                            // (and the chain is gone)
        Match careless;
        careless.init("ISLANDS", 1, 0x01, Level::Medium, 0, [&](LevelPlan& p) { lone(p); p.island_guard = false; });
        dig_north_chain(careless.sim, [&] { careless.tick(); });
        careless.run(4300);
        ASSERT_TRUE(careless.total_collapsed() + cut_off(careless) >= 1);
        ASSERT_EQ(careless.island(0).holds() + careless.island(0).recalls() + careless.island(0).escapes(), 0u);
    } TEST_END();

    TEST_CASE("AI15.8 An Ant That Stands On A Bridge That Is About To Go Is Sent Off It: A Worker Put On The Middle Tile Of The Chain With 700 Ticks Of Life Left Is Off The Bridge Within 400 Ticks (A Medium Bot, ISLANDS); Without The Guard It Stands There And Drowns")
    {
        const auto trial = [&](bool guard, bool* off_bridge, int* drowned) {
            Match m;
            m.init("ISLANDS", 1, 0x01, Level::Medium, 0, [&](LevelPlan& p) { lone(p); p.island_guard = guard; p.island_bridge_ants = 0; });
            const uint64_t middle = dig_north_chain(m.sim, [&] { m.tick(); });
            m.run(static_cast<int>(middle + 3600 - 700 - m.sim.current_tick()));                                           // (the middle tile has 700 ticks of life left)
            const uint32_t ant = m.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{24, 17});
            m.run(400);
            const sim::AntSnapshot* a = nullptr;
            for (const auto& s : m.sim.get_world_state().ants) {
                if (s.id == ant) a = &s;
            }
            *off_bridge = a != nullptr && !on_bridge(m.sim, *a);
            m.run(900);                                                                                                       // (the chain is gone by now)
            *drowned = m.total_collapsed();
        };
        bool off = false;
        int drowned = 0;
        trial(true, &off, &drowned);
        ASSERT_TRUE(off);
        ASSERT_EQ(drowned, 0);
        trial(false, &off, &drowned);
        ASSERT_FALSE(off);
        ASSERT_TRUE(drowned >= 1);
    } TEST_END();
}
