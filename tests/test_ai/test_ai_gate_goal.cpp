// AI27: the gate goals (docs/reverse_engineering/bomb_gates.md, "Gate goals"): what a Medium or Hard bot whose food is walled off by fire walls, bombs or rock sets out to do (plan_gate_goal), and how
// the task works it (GateGoalTask): the right ant first, then the wall put out or the Bomber's hop, with the owner's times (a step 90 s, an attempt 4 minutes, two retries). There is no switch: the bot plays
// it by itself on a walled map (GateInfo::walled at the first look, and not a map that the island play owns), never on any other map.
//
//   AI27.1   the planner on hand-made worlds: no goal where food is free, a wall to put out, a hop onto a wanted power-up, a hop in reach of a pile, the rules that hold a goal back
//   AI27.2   the planner on POPcOrN: the same goal for every seat of a side, a hop of two bombs onto the Swimmer at (1,15)
//   AI27.3b  an ant of the kind on the far side of the wall is no ant for the goal
//   AI27.3   a Hard bot puts a fire wall out with a Fire ant and banks the food behind it; the same world with a free pile is not played as a gate map
//   AI27.4   a Hard bot bombs itself over a rock wall onto a Swimmer power-up, also when a bomb is a dud, and takes it
//   AI27.4b  a chain of two bombs that stops at a dud: the attempt is given up as stranded
//   AI27.4c  a hurt Bomber, the planned throw of a step from each of the 8 directions, the fallback
//   AI27.5   the times: a goal that cannot be served is given up after a step of 90 s, three attempts at most, then the bot plays on and asks for nothing
//   AI27.5b  a task that ran out of attempts starts afresh when a source of its ant appears
//   AI27.6   who plays it: Medium and Hard on a walled map, not Easy, not a map with a free pile; the numbers of the plan are the owner's
//   AI27.7   a Bomber defuses a bomb that plugs a rock wall's tunnel; the pick-up trips avoid map bombs
//   AI27.7c  a bomb of the seat's own color that plugs a tunnel is defused like any other (act_defuse counts a bomb of any color at the goal cell as armed)
//   AI27.8   the sealed vault of HELLRAISER and the shore breach of POPcOrN, on the Rig
//   AI27.9   HELLRAISER through the REAL controller (the arena's play_match, with the filter that refuses a click on a power-up and a plant under an ant): each seat banks food over two walls
//   AI27.9b  the same with an idle own worker on the plant tile and on the tile to step from (the controller refuses an order onto an ant): they are sent aside and the first attempt banks
//   AI27.10  which maps are played as gate maps: HELLRAISER and POPcOrN at Medium and Hard, none of the shipped maps (ISLANDS is left to the island play), Bombz Away not
//   AI27.11  an open map is played as it was: the decision is taken once at the first look, a world with no pile is no gate map, and the pick-up trips keep their old rules (three candidates, the contest, the map bombs, the idle Swimmer); the contest is judged on a gate map at Hard, skipped at Medium
//   AI27.12  the plant tile and the tile to step from are cleared of own ants (sent aside, held until the goal is over)
//   AI27.12b an enemy ant on the tile to step from is waited for, not attacked (a plain move onto it is an attack)
//   AI27.13  the opening of a gate map: the trip to the Fire power-up of the own side is held back for 2,400 ticks while nothing is banked, then made; the Fire Ant is not put on wall duty
//   AI27.13b the opening puts no Fire Ant on wall duty (a thief power-up that the enemy reaches calls for walls on an open map, not on a gate map before it has banked)
//   AI27.13c an own worker beside a pile at the first look: the flag is set and the opening is not held back
//   AI27.14  on a gate map the counters leave the map's own bombs (of no team) alone while the gate goals are live; a team's bomb there, a map bomb on an open map and a map bomb once the
//            goals are exhausted are countered as before (every seat, straight and transposed)
#include <algorithm>

#include "ai_test.hpp"
#include "b41_helpers.hpp"
#include "ants_ai/arena.hpp"
#include "ants_ai/gate_goal.hpp"
#include "ants_ai/gate_task.hpp"

using namespace ai_test;
using namespace ants;
using namespace ants::ai;
using namespace b41;

namespace {

// A 60 x 60 field with the four hills, a pile at (12, 40) behind a wall across row 20, eight workers of seat 0 near its hill
struct Field {
    sim::SimulationEngine sim;
    explicit Field(uint32_t seed = 3, uint32_t ticks = 14400) {
        empty_field(sim, seed, ticks);
        place_crackers(sim, 12, 40, 25);
        for (int k = 0; k < 8; ++k) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{6 + k, 10});
    }
    void fire_wall() {
        for (int x = 0; x < 60; ++x) sim.set_fire_at(TileCoord{x, 20}, 1000000);
    }
    void bomb_wall() {
        for (int x = 0; x < 60; ++x) sim.grid_mut().place_bomb(static_cast<uint32_t>(x), 20, 1);
    }
    void rock_wall() {
        for (int x = 0; x < 60; ++x) sim.set_terrain(x, 20, sim::TERRAIN_OBSTACLE);
    }
    void powerup(int x, int y, sim::AntType kind) { sim.grid_mut().place_powerup(x, y, static_cast<uint8_t>(kind)); }
    GateGoal goal(const GoalRules& rules = GoalRules{}) {
        const MapInfo map(sim);
        const BotView view = BotView::build(sim, 0, &map);
        const GateInfo gates = GateInfo::analyse(map, sim.grid(), 0);
        return plan_gate_goal(gates, view, rules);
    }
};

LevelPlan goal_plan(Level level = Level::Hard) {
    return peace_plan(level);                                                      // (the gate play needs no switch: a walled world is enough)
}

LevelPlan shore_plan(Level level = Level::Hard) {                                  // the gate goals' own shore breach: the field breach off (AI29: it takes these piles when it is on)
    LevelPlan p = goal_plan(level);
    p.gate_field = false;
    return p;
}

int fires_in_row(const sim::SimulationEngine& sim, int y) {
    int n = 0;
    for (int x = 0; x < 60; ++x) n += sim.has_fire_at(TileCoord{x, y}) ? 1 : 0;
    return n;
}

}  // namespace

void run_gate_goal_tests() {
    TEST_CASE("AI27.1 The Planner On Hand-Made Worlds: No Goal Where Food Is Free, A Wall To Put Out, A Hop Onto A Wanted Power-Up, A Hop In Reach Of A Pile, The Rules That Hold A Goal Back") {
        {
            Field f;                                                                  // nothing in the way
            f.powerup(8, 8, sim::AntType::Fire);
            ASSERT_FALSE(f.goal().active());
        }
        {
            Field f;                                                                  // a fire wall across the world and a Fire power-up at home
            f.fire_wall();
            f.powerup(8, 8, sim::AntType::Fire);
            const GateGoal g = f.goal();
            ASSERT_EQ(g.kind, GoalKind::Extinguish);
            ASSERT_EQ(g.need, sim::AntType::Fire);
            ASSERT_EQ(g.cell.y, 20);
            ASSERT_EQ(g.cost, 110);
            ASSERT_EQ(g.gate, GateKind::Fire);
            ASSERT_FALSE(g.powerup);
        }
        {
            Field f;                                                                  // two fire walls, the pile behind both: two cells to cross, the one nearest the hill is the one to put out first
            f.fire_wall();
            for (int x = 0; x < 60; ++x) f.sim.set_fire_at(TileCoord{x, 30}, 1000000);
            f.powerup(8, 8, sim::AntType::Fire);
            const GateGoal g = f.goal();
            ASSERT_EQ(g.kind, GoalKind::Extinguish);
            ASSERT_EQ(g.cell.y, 20);
            ASSERT_EQ(g.cost, 120);
        }
        {
            Field f;                                                                  // a second pile on the hill's own side: food the hill reaches, so no goal for the one behind the wall
            f.fire_wall();
            place_crackers(f.sim, 30, 12, 25);
            f.powerup(8, 8, sim::AntType::Fire);
            ASSERT_FALSE(f.goal().active());
        }
        {
            Field f;                                                                  // a rock wall and a Swimmer behind it, but no Bomber anywhere: nothing to hop with, no goal
            f.rock_wall();
            f.powerup(30, 30, sim::AntType::Swimmer);
            ASSERT_FALSE(f.goal().active());
        }
        {
            Field f;                                                                  // the same wall, nothing to fight it with: no goal (a goal with no source would wait out its time)
            f.fire_wall();
            ASSERT_FALSE(f.goal().active());
        }
        {
            Field f;                                                                  // a line of bombs, a Bomber at home: a hop in reach of the pile (one bomb, 400 + 40)
            f.bomb_wall();
            f.powerup(8, 8, sim::AntType::Bomber);
            const GateGoal g = f.goal();
            ASSERT_EQ(g.kind, GoalKind::Hop);
            ASSERT_EQ(g.need, sim::AntType::Bomber);
            ASSERT_FALSE(g.powerup);
            ASSERT_EQ(g.cost, 440);
            ASSERT_EQ(g.hop.bombs, 1);
            ASSERT_TRUE(g.cell == g.hop.bomb && g.hop.land().y > 20);
        }
        {
            Field f;                                                                  // a rock wall, a Bomber, and a Swimmer power-up behind the wall: the power-up comes first (200 + 40)
            f.rock_wall();
            f.powerup(8, 8, sim::AntType::Bomber);
            f.powerup(30, 30, sim::AntType::Swimmer);
            const GateGoal g = f.goal();
            ASSERT_EQ(g.kind, GoalKind::Hop);
            ASSERT_TRUE(g.powerup);
            ASSERT_EQ(g.cost, 240);
            ASSERT_EQ(g.gate, GateKind::Sealed);
            ASSERT_TRUE(g.hop.land().y > 20 && g.hop.land().y <= 34);
            GoalRules thief_only;                                                       // a kind the bot does not want is no reason for a hop: the pile's hop (440) is the goal left
            thief_only.kinds = static_cast<uint8_t>(1u << static_cast<unsigned>(sim::AntType::Thief));
            const GateGoal p = f.goal(thief_only);
            ASSERT_TRUE(p.active() && !p.powerup);
            ASSERT_EQ(p.cost, 440);
            GoalRules none;                                                             // a hop of more bombs than the rules allow is not planned
            none.hop_bombs = 0;
            ASSERT_FALSE(f.goal(none).active());
        }
        {
            Field f;                                                                  // a Bomber only on the far side of the wall is no source (and one on the mound of the hill is)
            f.rock_wall();
            f.powerup(30, 30, sim::AntType::Swimmer);
            f.sim.spawn_unit(0, sim::AntType::Bomber, TileCoord{10, 30});
            ASSERT_FALSE(f.goal().active());
            f.sim.spawn_unit(0, sim::AntType::Bomber, TileCoord{5, 5});                     // a tile of the hill's mound: no component, the hill's own side
            ASSERT_TRUE(f.goal().active());
        }
        {
            Field f;                                                                  // a wall five rocks thick: no flight clears it, no goal
            for (int y = 18; y <= 22; ++y) {
                for (int x = 0; x < 60; ++x) f.sim.set_terrain(x, y, sim::TERRAIN_OBSTACLE);
            }
            f.powerup(8, 8, sim::AntType::Bomber);
            ASSERT_FALSE(f.goal().active());
        }
        {
            Field f;                                                                  // the fire wall and the bombs behind it: a Fire ant does not help (no walk), the Bomber's hop does
            f.fire_wall();
            for (int x = 0; x < 60; ++x) f.sim.grid_mut().place_bomb(static_cast<uint32_t>(x), 21, 1);
            f.powerup(8, 8, sim::AntType::Fire);
            f.powerup(9, 8, sim::AntType::Bomber);
            const GateGoal g = f.goal();
            ASSERT_EQ(g.kind, GoalKind::Hop);
            ASSERT_EQ(g.gate, GateKind::Mixed);
        }
    } TEST_END();

    TEST_CASE("AI27.2 The Planner On POPcOrN: The Same Goal For The Seats Of One Side, A Hop Of Two Bombs Onto The Swimmer At (1,15)") {
        assets::LevelData level;
        ASSERT_TRUE(level.load_from_file(std::string(ANTS_SOURCE_DIR) + "/Community-Maps/POPcOrN.lvl"));
        GateGoal goals[4];
        for (uint8_t seat = 0; seat < 4; ++seat) {
            sim::SimulationEngine sim;
            sim.init(level, 7, 0x0F);
            const MapInfo map(sim);
            const BotView view = BotView::build(sim, seat, &map);
            const GateInfo gates = GateInfo::analyse(map, sim.grid(), seat);
            goals[seat] = plan_gate_goal(gates, view, GoalRules{});
            const GateGoal& g = goals[seat];
            ASSERT_EQ(g.kind, GoalKind::Hop);
            ASSERT_EQ(g.need, sim::AntType::Bomber);
            ASSERT_TRUE(g.powerup);
            ASSERT_TRUE(map.powerups()[g.target].tile == (TileCoord{1, 15}) && map.powerups()[g.target].type == sim::AntType::Swimmer);
            ASSERT_EQ(g.hop.bombs, 2);
            ASSERT_EQ(g.cost, 280);
            ASSERT_TRUE(g.hop.land() == (TileCoord{1, 15}));
            ASSERT_EQ(g.hop.hp, 4);
            ASSERT_EQ(g.hop.fly_percent, 64);
            ASSERT_EQ(g.gate, GateKind::Sealed);                                       // no walk reaches it, even through fire and bombs together
        }
        {
            sim::SimulationEngine sim;                                                  // an enemy ant stands on the Swimmer power-up: nobody else can take it, so the goal is another
            sim.init(level, 7, 0x0F);
            sim.spawn_unit(1, sim::AntType::Worker, TileCoord{1, 15});
            const MapInfo map(sim);
            const BotView view = BotView::build(sim, 0, &map);
            ASSERT_TRUE(view.powerup_at(TileCoord{1, 15}) != nullptr && view.powerup_at(TileCoord{1, 15})->standing_team == 1u);
            const GateGoal g = plan_gate_goal(GateInfo::analyse(map, sim.grid(), 0), view, GoalRules{});
            ASSERT_TRUE(!g.active() || !g.powerup || !(map.powerups()[g.target].tile == (TileCoord{1, 15})));
        }
        ASSERT_TRUE(goals[0].cell == (TileCoord{9, 7}) && goals[2].cell == goals[0].cell && goals[2].target == goals[0].target);       // the two north seats, the two south seats: the same tile, mirrored (rows 7 and 23)
        ASSERT_TRUE(goals[1].cell == (TileCoord{9, 23}) && goals[3].cell == goals[1].cell && goals[3].target == goals[1].target);
        ASSERT_TRUE(goals[0].hop.lands.size() == 2 && goals[0].hop.lands[0] == (TileCoord{5, 11}));
        {
            sim::SimulationEngine sim;                                                  // a kind whose best hop lands in reach of the power-up but not on it: the chain that lands ON it is the one planned
            sim.init(level, 7, 0x0F);
            const MapInfo map(sim);
            const BotView view = BotView::build(sim, 0, &map);
            GoalRules thief;
            thief.kinds = static_cast<uint8_t>(1u << static_cast<unsigned>(sim::AntType::Thief));
            thief.hop_bombs = 4;
            const GateGoal g = plan_gate_goal(GateInfo::analyse(map, sim.grid(), 0), view, thief);
            ASSERT_TRUE(g.active() && g.powerup);
            ASSERT_TRUE(g.hop.land() == map.powerups()[g.target].tile);
            ASSERT_EQ(g.hop.bombs, 4);
        }
    } TEST_END();

    TEST_CASE("AI27.3 A Hard Bot Puts A Fire Wall Out With A Fire Ant And Banks The Food Behind It; The Same World With A Pile The Hill Reaches Is Not Played As A Gate Map") {
        for (int open = 0; open < 2; ++open) {
            Field f;
            f.fire_wall();
            f.powerup(8, 8, sim::AntType::Fire);
            if (open != 0) place_crackers(f.sim, 30, 12, 25);                          // food that the hill reaches: an open map
            Rig rig(f.sim, 0, Level::Hard, std::make_unique<StandardBot>(peace_plan(Level::Hard)));
            auto& bot = rig.as<StandardBot>();
            rig.run(3000);
            if (open != 0) {
                ASSERT_FALSE(bot.gate_map());
                ASSERT_EQ(bot.gate_goals().attempts(), 0u);
                ASSERT_EQ(fires_in_row(f.sim, 20), 60);                                // the wall stands: the bot eats the free pile
                ASSERT_TRUE(f.sim.get_player_score(0) > 0);
                continue;
            }
            ASSERT_TRUE(bot.gate_map());
            ASSERT_EQ(bot.gate_goals().extinguishes(), 1u);
            ASSERT_EQ(bot.gate_goals().done(), 1u);
            ASSERT_EQ(bot.gate_goals().given_up(), 0u);
            ASSERT_EQ(fires_in_row(f.sim, 20), 59);                                    // one cell is out: the way
            ASSERT_TRUE(f.sim.get_player_score(0) > 0);
            ASSERT_FALSE(bot.gate_goals().goal().active());                            // done: normal play
            ASSERT_EQ(bot.gate_goals().phase(), GateGoalTask::Phase::Idle);
            ASSERT_EQ(bot.ledger().count(StandardBot::kGateGoals), 0u);                // and no ant is held
        }
    } TEST_END();

    TEST_CASE("AI27.3b An Ant Of The Kind That Stands On The Far Side Of The Wall Is No Ant For The Goal: A Second One Is Fetched At Home") {
        Field f;
        f.rock_wall();
        f.powerup(8, 8, sim::AntType::Bomber);
        f.powerup(30, 30, sim::AntType::Swimmer);
        const uint32_t far = f.sim.spawn_unit(0, sim::AntType::Bomber, TileCoord{10, 30});     // a Bomber that cannot come to the wall's near side
        Rig rig(f.sim, 0, Level::Hard, std::make_unique<StandardBot>(goal_plan()));
        auto& bot = rig.as<StandardBot>();
        rig.run(1500);
        ASSERT_EQ(bot.gate_goals().done(), 1u);                                           // the goal was done with the Bomber of the near side
        ASSERT_TRUE(bot.gate_goals().plants() >= 1u);
        const sim::AntSnapshot* snapshot = nullptr;
        for (const sim::AntSnapshot& a : f.sim.get_world_state().ants) {
            if (a.id == far) snapshot = &a;
        }
        ASSERT_TRUE(snapshot != nullptr && snapshot->type == sim::AntType::Bomber);          // the far one stayed what it was, where it was
    } TEST_END();

    TEST_CASE("AI27.3c A Bomber Too Hurt For The Chain Is Not Used (A Chain Of One Bomb Costs Two Hit Points, The Ant Needs Three), Another Is Fetched; With Less Than 2,400 Ticks On The Clock No Attempt Starts") {
        {
            Field f;
            f.rock_wall();
            f.powerup(8, 8, sim::AntType::Bomber);
            f.powerup(30, 30, sim::AntType::Swimmer);
            const uint32_t hurt = f.sim.spawn_unit(0, sim::AntType::Bomber, TileCoord{12, 10});
            f.sim.get_unit(hurt).hp = 2;
            Rig rig(f.sim, 0, Level::Hard, std::make_unique<StandardBot>(goal_plan()));
            auto& bot = rig.as<StandardBot>();
            uint8_t most_wanted = 0;
            for (int k = 0; k < 120; ++k) {
                rig.run(10);
                most_wanted = std::max(most_wanted, bot.tactics().wants[static_cast<size_t>(sim::AntType::Bomber)]);
            }
            ASSERT_TRUE(most_wanted >= 2u);                                                // one more than the ants that cannot serve
            rig.run(600);
            ASSERT_EQ(bot.gate_goals().done(), 1u);
            ASSERT_EQ(bot.gate_goals().given_up(), 0u);
            ASSERT_TRUE(f.sim.get_unit(hurt).pos.y < 20 && f.sim.get_unit(hurt).type == sim::AntType::Bomber);   // the hurt one did not hop
        }
        {
            Field f(3, 2000);                                                              // a match with 2,000 ticks left: too little for a gate
            f.fire_wall();
            f.powerup(8, 8, sim::AntType::Fire);
            Rig rig(f.sim, 0, Level::Hard, std::make_unique<StandardBot>(goal_plan()));
            rig.run(600);
            ASSERT_EQ(rig.as<StandardBot>().gate_goals().attempts(), 0u);
            ASSERT_EQ(fires_in_row(f.sim, 20), 60);
        }
    } TEST_END();

    TEST_CASE("AI27.4 A Hard Bot With The Goal On Bombs Itself Over A Rock Wall Onto A Swimmer Power-Up And Takes It: A Dud Is Planted Again, The Pick-Up Is A Move That Ends On The Tile (Seeds 1 .. 4)") {
        uint32_t duds = 0;
        for (uint32_t seed = 1; seed <= 4; ++seed) {
            Field f(seed);
            f.rock_wall();
            f.powerup(8, 8, sim::AntType::Bomber);
            f.powerup(30, 30, sim::AntType::Swimmer);
            Rig rig(f.sim, 0, Level::Hard, std::make_unique<StandardBot>(goal_plan()));
            auto& bot = rig.as<StandardBot>();
            rig.run(2400);
            ASSERT_EQ(bot.gate_goals().done(), 1u);
            ASSERT_EQ(bot.gate_goals().hops_ordered() >= 1u, true);
            ASSERT_TRUE(bot.gate_goals().plants() >= 1u);
            duds += bot.gate_goals().duds();
            bool swimmer_behind_wall = false;
            for (const sim::AntSnapshot& a : f.sim.get_world_state().ants) {
                if (a.player_id == 0 && a.type == sim::AntType::Swimmer && a.tile_y > 20) swimmer_behind_wall = true;
            }
            ASSERT_TRUE(swimmer_behind_wall);
            ASSERT_FALSE(f.sim.grid().has_powerup_at(TileCoord{30, 30}));
            ASSERT_EQ(bot.ledger().count(StandardBot::kGateGoals), 0u);
            ASSERT_FALSE(bot.gate_goals().goal().active());                            // done: the Bomber is a Swimmer now and the Bomber power-up it left behind lies beyond the wall, so there is no goal left
        }
        ASSERT_TRUE(duds >= 1u);                                                       // a bomb in five is a dud: over the four seeds one was, and the bot planted again
    } TEST_END();

    TEST_CASE("AI27.4b A Chain Of Two Bombs That Stops At A Dud Leaves The Bomber On The Far Side: The Attempt Is Given Up As Stranded, And That Ant Is No Ant For The Next One; A Chain That Does Not Stop Is Done (Seeds 1 .. 16)") {
        uint32_t stranded = 0, done = 0;
        for (uint32_t seed = 1; seed <= 16; ++seed) {
            Field f(seed);
            f.rock_wall();
            for (int y = 21; y <= 24; ++y) {
                for (int x = 0; x < 60; ++x) f.sim.grid_mut().place_bomb(static_cast<uint32_t>(x), static_cast<uint32_t>(y), 1);   // map bombs behind the wall: every flight lands on one
            }
            f.powerup(8, 8, sim::AntType::Bomber);
            f.powerup(30, 34, sim::AntType::Swimmer);
            Rig rig(f.sim, 0, Level::Hard, std::make_unique<StandardBot>(goal_plan()));
            auto& bot = rig.as<StandardBot>();
            bool counted = false;
            for (int t = 0; t < 1200; ++t) {
                rig.run(1);
                if (!counted && bot.gate_goals().given_up() >= 1u) {
                    counted = true;
                    ASSERT_EQ(bot.ledger().count(StandardBot::kGateGoals), 0u);              // the ant of a failed attempt is given back at once
                }
            }
            done += bot.gate_goals().done();
            if (std::string(bot.gate_goals().last_failure()) == "stranded") ++stranded;
        }
        ASSERT_TRUE(stranded >= 1u);                                                    // a dud on the second bomb (a fifth of the throws) happened in these seeds
        ASSERT_TRUE(done >= 1u);                                                        // and a chain that flew on
    } TEST_END();

    TEST_CASE("AI27.4c A Bomber That Is Hurt On The Way To The Plant Gives The Attempt Up As Hurt; A Diagonal Step Onto An Own Bomb Is Thrown As Planned; The Bot's Fallback Gives The Ant Back") {
        {
            Field f;
            f.rock_wall();
            for (int y = 21; y <= 24; ++y) {
                for (int x = 0; x < 60; ++x) f.sim.grid_mut().place_bomb(static_cast<uint32_t>(x), static_cast<uint32_t>(y), 1);   // a chain of two bombs: five hit points to plant
            }
            f.powerup(8, 8, sim::AntType::Bomber);
            f.powerup(30, 34, sim::AntType::Swimmer);
            Rig rig(f.sim, 0, Level::Hard, std::make_unique<StandardBot>(goal_plan()));
            auto& bot = rig.as<StandardBot>();
            bool hurt = false;
            for (int t = 0; t < 1500 && bot.gate_goals().given_up() == 0u; ++t) {
                rig.run(1);
                if (!hurt && bot.gate_goals().phase() == GateGoalTask::Phase::Act && bot.gate_goals().ant() != 0u) {
                    f.sim.get_unit(bot.gate_goals().ant()).hp = 3;                         // hurt before the first plant
                    hurt = true;
                }
            }
            ASSERT_TRUE(hurt);
            ASSERT_EQ(std::string(bot.gate_goals().last_failure()), "hurt");
            ASSERT_EQ(bot.gate_goals().plants(), 0u);
        }
        for (int f = 0; f < 8; ++f) {                                                   // the planned direction of a hop is the engine's: an ant on the tile next to the bomb, in each of the 8 directions
            sim::SimulationEngine sim;
            empty_field(sim, 3, 14400);
            const sim::movement::TileDelta d = sim::movement::dir_delta(static_cast<uint8_t>(f));
            const TileCoord bomb{30, 30}, from{bomb.x - d.dcol, bomb.y - d.drow};
            sim.grid_mut().place_bomb(30, 30, 0);
            const uint32_t id = sim.spawn_unit(0, sim::AntType::Bomber, from);
            sim.issue_move_order(id, bomb);
            for (int t = 0; t < 120 && sim.grid().has_bomb_at(bomb); ++t) sim.tick();
            ASSERT_TRUE(sim.get_unit(id).flight_tile == GateInfo::simulate(sim.grid(), bomb, static_cast<uint8_t>(f)).lands[0]);
        }
        {
            Field f;                                                                  // the fallback: the held ant is given back, no failure is counted
            f.rock_wall();
            f.powerup(8, 8, sim::AntType::Bomber);
            f.powerup(30, 30, sim::AntType::Swimmer);
            Rig rig(f.sim, 0, Level::Hard, std::make_unique<StandardBot>(goal_plan()));
            auto& bot = rig.as<StandardBot>();
            for (int t = 0; t < 1500 && !(bot.gate_goals().phase() == GateGoalTask::Phase::Act && bot.gate_goals().ant() != 0u); ++t) rig.run(1);
            ASSERT_EQ(bot.ledger().count(StandardBot::kGateGoals), 1u);
            bot.gate_goals_for_labs().cancel(bot.ledger_for_labs());
            ASSERT_EQ(bot.ledger().count(StandardBot::kGateGoals), 0u);
            ASSERT_EQ(bot.gate_goals().phase(), GateGoalTask::Phase::Idle);
            ASSERT_EQ(bot.gate_goals().given_up(), 0u);
            rig.run(1500);
            ASSERT_EQ(bot.gate_goals().done(), 1u);                                      // and it starts afresh when the bot is back
        }
    } TEST_END();

    TEST_CASE("AI27.4d When The Bot Falls Back To The Plain Economy (The Stall Detector) The Ant Of A Gate Goal Is Given Back At That Look") {
        Field f;
        std::vector<uint32_t> workers;                                                  // no worker: nobody harvests while the Bomber is held, so nothing is banked
        for (const sim::AntSnapshot& a : f.sim.get_world_state().ants) workers.push_back(a.id);
        for (const uint32_t id : workers) f.sim.kill_unit(id);
        f.sim.spawn_unit(0, sim::AntType::Bomber, TileCoord{10, 14});
        f.bomb_wall();                                                                  // walkable for the map analysis, so the pile counts as reachable and a stall is possible
        LevelPlan plan = goal_plan();
        plan.stall_ticks = 60;
        plan.gate_step_ticks = 100000;                                                    // the goal is not given up by its own clock before the stall
        plan.gate_attempt_ticks = 100000;
        Rig rig(f.sim, 0, Level::Hard, std::make_unique<StandardBot>(plan));
        auto& bot = rig.as<StandardBot>();
        bool held_before = false;
        for (int t = 0; t < 600 && !bot.in_fallback(); ++t) {
            rig.run(1);
            held_before = held_before || bot.ledger().count(StandardBot::kGateGoals) != 0u;
        }
        ASSERT_TRUE(bot.in_fallback());
        ASSERT_TRUE(held_before);                                                       // the task held an ant when the fallback began (or just before)
        ASSERT_EQ(bot.ledger().count(StandardBot::kGateGoals), 0u);
        ASSERT_EQ(bot.gate_goals().given_up(), 0u);                                      // and the cancel counts as no failure
        ASSERT_EQ(bot.gate_goals().phase(), GateGoalTask::Phase::Idle);
    } TEST_END();

    TEST_CASE("AI27.4e The Gate Goal Takes Its Ant From A Fight (Rank 6 Above 5); A Source That Appears Later Starts The Search; The Number Of Goals Is Limited") {
        {
            Field f;
            f.rock_wall();
            f.powerup(30, 30, sim::AntType::Swimmer);
            const uint32_t bomber = f.sim.spawn_unit(0, sim::AntType::Bomber, TileCoord{10, 14});
            Rig rig(f.sim, 0, Level::Hard, std::make_unique<StandardBot>(goal_plan()));
            auto& bot = rig.as<StandardBot>();
            ASSERT_TRUE(bot.ledger_for_labs().take(bomber, StandardBot::kFight));         // the Bomber is in a fight
            rig.run(1500);
            ASSERT_EQ(bot.gate_goals().done(), 1u);
        }
        {
            Field f;
            f.rock_wall();
            f.powerup(30, 30, sim::AntType::Swimmer);                                      // beyond the wall, and nothing at home to hop with: no goal
            Rig rig(f.sim, 0, Level::Hard, std::make_unique<StandardBot>(goal_plan()));
            auto& bot = rig.as<StandardBot>();
            rig.run(1000);
            ASSERT_EQ(bot.gate_goals().attempts(), 0u);
            f.powerup(8, 8, sim::AntType::Bomber);                                         // a source appears: the task looks again
            rig.run(1500);
            ASSERT_EQ(bot.gate_goals().done(), 1u);
        }
        {
            Field f;
            f.fire_wall();
            f.powerup(8, 8, sim::AntType::Fire);
            Rig rig(f.sim, 0, Level::Hard, std::make_unique<StandardBot>(goal_plan()));
            auto& bot = rig.as<StandardBot>();
            GateGoalTask::Params params = bot.gate_goals().params();
            params.goals_max = 1;
            bot.gate_goals_for_labs().set_params(params);
            rig.run(2500);
            ASSERT_EQ(bot.gate_goals().done(), 1u);
            ASSERT_TRUE(bot.gate_goals().exhausted());                                     // one goal was all that was allowed
            f.powerup(9, 8, sim::AntType::Fire);
            for (int x = 0; x < 60; ++x) f.sim.set_fire_at(TileCoord{x, 30}, 1000000);       // and a second wall afterwards is left alone
            rig.run(1500);
            ASSERT_EQ(bot.gate_goals().attempts(), 1u);
        }
    } TEST_END();

    TEST_CASE("AI27.5b A Task That Ran Out Of Attempts Starts Afresh When A Source Of Its Ant Appears (At Most Twice): A New Fire Power-Up On The Hill's Side After Three Attempts Is Put To Use") {
        for (uint32_t revivals_max = 0; revivals_max <= 2; revivals_max += 2) {
            Field f;
            f.fire_wall();
            f.powerup(30, 10, sim::AntType::Fire);
            f.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{30, 10});                // nobody can take that one
            Rig rig(f.sim, 0, Level::Hard, std::make_unique<StandardBot>(goal_plan()));
            auto& bot = rig.as<StandardBot>();
            GateGoalTask::Params params = bot.gate_goals().params();
            params.revivals_max = revivals_max;
            bot.gate_goals_for_labs().set_params(params);
            rig.run(7000);
            ASSERT_TRUE(bot.gate_goals().exhausted());
            f.powerup(9, 12, sim::AntType::Fire);                                          // a Fire power-up that a worker can take
            rig.run(2500);
            if (revivals_max == 0) {
                ASSERT_TRUE(bot.gate_goals().exhausted());
                ASSERT_EQ(bot.gate_goals().revivals(), 0u);
                ASSERT_EQ(fires_in_row(f.sim, 20), 60);
            } else {
                ASSERT_EQ(bot.gate_goals().revivals(), 1u);
                ASSERT_EQ(bot.gate_goals().done(), 1u);
                ASSERT_EQ(fires_in_row(f.sim, 20), 59);
                ASSERT_TRUE(f.sim.get_player_score(0) > 0);
            }
        }
    } TEST_END();

    TEST_CASE("AI27.5 The Times: A Goal That Cannot Be Served Is Given Up After A Step Of 90 Seconds, At Most Three Attempts, Then The Bot Plays On And Asks For Nothing") {
        Field f;
        f.fire_wall();
        f.powerup(30, 10, sim::AntType::Fire);                                         // a Fire power-up that nobody can take: an enemy ant stands on it (nobody else can take it then)
        f.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{30, 10});
        Rig rig(f.sim, 0, Level::Hard, std::make_unique<StandardBot>(goal_plan()));
        auto& bot = rig.as<StandardBot>();
        const GateGoalTask::Params defaults = bot.gate_goals().params();
        ASSERT_EQ(defaults.step_ticks, 1800u);                                         // 90 s of 50 ms ticks
        ASSERT_EQ(defaults.attempt_ticks, 4800u);                                      // 4 minutes
        ASSERT_EQ(defaults.retries, 2u);
        rig.run(1700);
        ASSERT_EQ(bot.gate_goals().attempts(), 1u);
        ASSERT_EQ(bot.gate_goals().given_up(), 0u);                                    // not yet: the step has 100 ticks left
        ASSERT_EQ(bot.gate_goals().phase(), GateGoalTask::Phase::Fetch);
        ASSERT_TRUE(bot.tactics().wants[static_cast<size_t>(sim::AntType::Fire)] >= 1u);   // the right ant is asked for
        uint32_t until_fail = 0;
        while (bot.gate_goals().given_up() == 0u && until_fail < 600u) {
            rig.run(1);
            ++until_fail;
        }
        ASSERT_EQ(bot.gate_goals().given_up(), 1u);                                    // after the step: the first attempt is over
        uint32_t waited = 0;
        while (bot.gate_goals().attempts() < 2u && waited < 400u) {                    // the next one starts after the pause of 100 ticks, at the next look (20 ticks)
            rig.run(1);
            ++waited;
        }
        ASSERT_TRUE(waited >= 100u && waited <= 125u);
        rig.run(4700);
        ASSERT_EQ(bot.gate_goals().attempts(), 3u);                                    // the first and two retries
        ASSERT_EQ(bot.gate_goals().given_up(), 3u);
        ASSERT_TRUE(bot.gate_goals().exhausted());
        ASSERT_EQ(bot.tactics().wants[static_cast<size_t>(sim::AntType::Fire)], 0u);    // and nothing is asked for any more
        rig.run(3000);
        ASSERT_EQ(bot.gate_goals().attempts(), 3u);
        ASSERT_EQ(bot.ledger().count(StandardBot::kGateGoals), 0u);
        ASSERT_EQ(fires_in_row(f.sim, 20), 60);
        // the owner's numbers are the plan's: tuned, they change the task
        LevelPlan quick = goal_plan();
        quick.gate_step_ticks = 300;
        quick.gate_retries = 0;
        Field g;
        g.fire_wall();
        g.powerup(30, 10, sim::AntType::Fire);
        g.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{30, 10});
        Rig quick_rig(g.sim, 0, Level::Hard, std::make_unique<StandardBot>(quick));
        quick_rig.run(800);
        ASSERT_TRUE(quick_rig.as<StandardBot>().gate_goals().exhausted());
        ASSERT_EQ(quick_rig.as<StandardBot>().gate_goals().attempts(), 1u);
        ASSERT_EQ(std::string(quick_rig.as<StandardBot>().gate_goals().last_failure()), "step");
        // the attempt's own limit: a step limit out of reach, an attempt limit of 600 ticks
        LevelPlan slow = goal_plan();
        slow.gate_step_ticks = 100000;
        slow.gate_attempt_ticks = 600;
        slow.gate_retries = 0;
        Field h;
        h.fire_wall();
        h.powerup(30, 10, sim::AntType::Fire);
        h.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{30, 10});
        Rig slow_rig(h.sim, 0, Level::Hard, std::make_unique<StandardBot>(slow));
        slow_rig.run(1000);
        ASSERT_TRUE(slow_rig.as<StandardBot>().gate_goals().exhausted());
        ASSERT_EQ(std::string(slow_rig.as<StandardBot>().gate_goals().last_failure()), "attempt");
    } TEST_END();

    TEST_CASE("AI27.6 Who Plays It: Medium And Hard On A Walled Map, Not Easy, Not A Map With A Free Pile; The Numbers Of The Plan Are The Owner's") {
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            const LevelPlan plan = plan_for(level);
            ASSERT_EQ(plan.gate_step_ticks, 1800u);
            ASSERT_EQ(plan.gate_attempt_ticks, 4800u);
            ASSERT_EQ(plan.gate_retries, 2u);
        }
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            Field f;
            f.fire_wall();
            f.powerup(8, 8, sim::AntType::Fire);
            Rig rig(f.sim, 0, level, std::make_unique<StandardBot>(goal_plan(level)));
            rig.run(1500);
            auto& bot = rig.as<StandardBot>();
            if (level == Level::Easy) {
                ASSERT_FALSE(bot.gate_map());
                ASSERT_EQ(bot.gate_goals().attempts(), 0u);
                ASSERT_EQ(fires_in_row(f.sim, 20), 60);                                // Easy leaves the wall alone
            } else {
                ASSERT_TRUE(bot.gate_map());
                ASSERT_EQ(bot.gate_goals().extinguishes(), 1u);
                ASSERT_EQ(fires_in_row(f.sim, 20), 59);
            }
        }
    } TEST_END();

    TEST_CASE("AI27.7 Defuse Bomb Tunnel And Map Bomb Avoidance: A Hard Bot Defuses A Bomb Plugging A Rock-Wall Tunnel With A Bomber To Bank The Food Behind It, And PowerUpTask Routes Around Map Bombs") {
        {   // (a) a thick rock barrier (rows 15..25) with a staircase tunnel plugged by a map bomb at (12, 20): no 4-tile hop crosses it, so Defuse is planned and executed
            Field f;
            for (int y = 15; y <= 25; ++y) {
                const int tx = 10 + (y - 15) / 2;
                for (int x = 0; x < 60; ++x) {
                    const bool open = (y == 20) ? (x == 12) : (x == tx || x == tx + 1);
                    if (!open) f.sim.set_terrain(x, y, sim::TERRAIN_OBSTACLE);
                }
            }
            f.sim.grid_mut().place_bomb(12, 20, 255);
            f.powerup(8, 8, sim::AntType::Bomber);
            const GateGoal g = f.goal();
            ASSERT_EQ(g.kind, GoalKind::Defuse);
            ASSERT_EQ(g.need, sim::AntType::Bomber);
            ASSERT_EQ(g.cell.x, 12);
            ASSERT_EQ(g.cell.y, 20);
            Rig rig(f.sim, 0, Level::Hard, std::make_unique<StandardBot>(goal_plan()));
            rig.run(2500);
            ASSERT_FALSE(f.sim.grid().has_bomb_at(TileCoord{12, 20}));
            ASSERT_EQ(rig.as<StandardBot>().gate_goals().done(), 1u);
            ASSERT_TRUE(f.sim.get_player_score(0) > 0);
        }
        {   // (b) PowerUpTask::field_for avoids map bombs when the bot plays a gate map: a closer Fire power-up behind a map-bombed doorway is skipped in favour of the bomb-free one
            Field f;
            f.fire_wall();
            for (int y = 6; y <= 10; ++y) {
                for (int x = 15; x <= 19; ++x) {
                    const bool border = x == 15 || x == 19 || y == 6 || y == 10;
                    if (border && !(x == 15 && y == 8)) f.sim.set_terrain(x, y, sim::TERRAIN_OBSTACLE);
                }
            }
            f.sim.grid_mut().place_bomb(15, 8, 255);                                    // map bomb plugs the only doorway into the box
            f.powerup(17, 8, sim::AntType::Fire);                                       // closer by Chebyshev distance (3 tiles from (14, 8)), but behind the bomb
            f.powerup(12, 16, sim::AntType::Fire);                                      // 6 tiles away in open ground
            f.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{14, 8});
            ASSERT_EQ(f.goal().kind, GoalKind::Extinguish);
            Rig rig(f.sim, 0, Level::Hard, std::make_unique<StandardBot>(goal_plan()));
            rig.run(250);
            ASSERT_EQ(rig.as<StandardBot>().powerups().taken(), 1u);
            ASSERT_TRUE(f.sim.grid().has_bomb_at(TileCoord{15, 8}));
        }
        {   // (c) a bomb of the SEAT'S OWN color (every bomb of Dustbin, Ghetto, FKD Deathmatch and !-Midz-! is green, team 0's): the engine refuses a bomb tile as a path step for the owner's team, so for the
            // owner the bomb is a wall, and the engine defuses a bomb of any color. A lake (rows 30 to 34) between the hill and the pile, its shore held by own bombs: the shore breach plans a Defuse, and the
            // Bomber has to defuse the bomb (the task once took a bomb of its own color for defused and ordered it again for 4,600 ticks)
            Field f;
            for (int y = 30; y <= 34; ++y) {
                for (int x = 0; x < 60; ++x) f.sim.set_terrain(x, y, sim::TERRAIN_WATER);
            }
            for (int x = 0; x < 60; ++x) f.sim.grid_mut().place_bomb(static_cast<uint32_t>(x), 29, 0);
            f.powerup(8, 8, sim::AntType::Bomber);
            GoalRules rules;
            rules.shore_breach = true;
            const GateGoal g = f.goal(rules);
            ASSERT_EQ(g.kind, GoalKind::Defuse);
            ASSERT_EQ(g.cell.y, 29);
            Rig rig(f.sim, 0, Level::Hard, std::make_unique<StandardBot>(shore_plan()));
            auto& bot = rig.as<StandardBot>();
            rig.run(2500);
            ASSERT_TRUE(bot.gate_goals().done() >= 1u);                                // the Bomber defused a bomb of its own color ...
            ASSERT_TRUE(bot.gate_goals().defuses() < 50u);                             // ... and did not count the same bomb as defused look after look
            int left = 0;
            for (int x = 0; x < 60; ++x) left += f.sim.grid().has_bomb_at(TileCoord{x, 29}) ? 1 : 0;
            ASSERT_TRUE(left < 60);
        }
    } TEST_END();

    TEST_CASE("AI27.8 Sealed Vault Raid And Shore Breach: Hard Bots Bank Food On HELLRAISER And POPcOrN") {
        {   // (a) HELLRAISER: food pile sealed in central vault; a Hard Bomber hops in from its starting component, grabs food, and hops to its anthill component to bank it
            assets::LevelData level;
            ASSERT_TRUE(level.load_from_file(std::string(ANTS_SOURCE_DIR) + "/Community-Maps/HELLRAISER.lvl"));
            sim::SimulationEngine sim;
            sim.init(level, 1, 0x01);                                                  // (the team alone: the other teams' Combat Ants, parked in its home quadrant, knock a landing carrier back over the wall; AI27.9 plays the full roster)
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(goal_plan()));
            rig.run(2400);
            ASSERT_TRUE(rig.as<StandardBot>().gate_goals().hops_ordered() >= 2u);
            ASSERT_TRUE(sim.get_player_score(0) > 0);
        }
        {   // (b) POPcOrN with the field breach off (AI29.9 plays it on): Bomber hops to island Fire hat, bounces back, extinguishes shore fire, converts to Bomber, defuses shore bombs,
            // hops to island Swimmer hat, and banks food
            assets::LevelData level;
            ASSERT_TRUE(level.load_from_file(std::string(ANTS_SOURCE_DIR) + "/Community-Maps/POPcOrN.lvl"));
            sim::SimulationEngine sim;
            sim.init(level, 1, 0x01);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(shore_plan()));
            auto& bot = rig.as<StandardBot>();
            for (int step = 0; step < 25; ++step) {
                rig.run(200);
                if (sim.get_player_score(0) > 0) break;
            }
            ASSERT_TRUE(bot.gate_goals().extinguishes() >= 1u);
            ASSERT_TRUE(bot.gate_goals().defuses() >= 1u);
            ASSERT_TRUE(sim.get_player_score(0) > 0);
        }
    } TEST_END();
    // The Rig above applies what a bot proposes with no filter. The controller of a game (and of the arena) refuses a click on a power-up tile and a special order onto a tile with an ant: the hop of
    // HELLRAISER's vault used a power-up tile as the tile to step from (the Combat power-up at (13,13) for the way back to the north-west) and the start ants stand on the plant tile of the best
    // hop (the north-west team's three ants stand on (9,9)), so through the controller the bots did not bank at all.
    TEST_CASE("AI27.9 HELLRAISER Through The Real Controller: Each Seat Alone Banks Food Over Two Walls At Hard And Medium (The Tile To Step From Holds No Power-Up, The Plant Tile Is Cleared Of Own Ants); Easy Plays As It Did") {
        assets::LevelData level;
        ASSERT_TRUE(level.load_from_file(std::string(ANTS_SOURCE_DIR) + "/Community-Maps/HELLRAISER.lvl"));
        for (uint8_t seat = 0; seat < 4; ++seat) {
            for (const Level lvl : {Level::Hard, Level::Medium, Level::Easy}) {
                ArenaSpec spec;
                spec.level = &level;
                spec.seed = 1;
                spec.max_ticks = 6000;
                BotSpec b;
                b.seat = seat;
                b.kind = "standard";
                b.level = lvl;
                spec.bots.push_back(b);
                const ArenaResult r = play_match(spec);
                ASSERT_TRUE(r.error.empty());
                ASSERT_EQ(r.seats.size(), 1u);
                if (lvl == Level::Easy) {
                    ASSERT_EQ(r.seats[0].score, 0);                                    // Easy has no gate play: nothing is banked, as before
                } else {
                    ASSERT_TRUE(r.seats[0].score >= 100);                              // two deposits at least in 6,000 ticks (the measured: 4)
                    ASSERT_TRUE(r.seats[0].bombs_planted >= 4u);                       // the way in and the way back, twice
                }
            }
        }
        // and with the enemy on the map: the Bomber power-up in the north-west corner is the other hill's by the walk (power_up_side), and the start ants of the south-east hill's team stand beside it. A Medium
        // bot takes it all the same on a gate map (Medium does not steal on an open one), and banks over the walls with the other team's Combat Ants in its quadrant
        for (const uint32_t seed : {1u, 12u}) {
            ArenaSpec spec;
            spec.level = &level;
            spec.seed = seed;
            spec.max_ticks = 6000;
            for (uint8_t seat = 0; seat < 2; ++seat) {
                BotSpec b;
                b.seat = seat;
                b.kind = "standard";
                b.level = Level::Medium;
                spec.bots.push_back(b);
            }
            const ArenaResult r = play_match(spec);
            ASSERT_TRUE(r.error.empty());
            ASSERT_TRUE(r.seats[1].score >= 100);
        }
    } TEST_END();

    TEST_CASE("AI27.9b HELLRAISER Through The Real Controller With Own Ants On The Plant Tile And The Tile To Step From: The Controller Refuses An Order Onto An Ant, So The Bot Sends Them Aside, And Banks At Once, With No Attempt Given Up") {
        assets::LevelData level;
        ASSERT_TRUE(level.load_from_file(std::string(ANTS_SOURCE_DIR) + "/Community-Maps/HELLRAISER.lvl"));
        for (const uint32_t seed : {1u, 2u}) {
            sim::SimulationEngine sim;
            sim.init(level, seed, 0x01);                                               // the team alone (AI27.9 plays the others)
            RecordingSink sink(sim, true);
            BotController ctl(sim, seed);
            BotSpec spec;
            spec.seat = 0;
            spec.kind = "standard";
            spec.level = Level::Hard;
            auto inner = std::make_unique<StandardBot>(spec.level, spec.style);
            StandardBot* bot = inner.get();
            std::string why;
            ASSERT_TRUE(ctl.add(spec, std::move(inner), sink, why));
            bool placed = false;
            uint64_t first_bank = 0;
            uint32_t given_up_then = 0;
            for (uint64_t t = 0; t < 6000; ++t) {
                sim.tick();
                sim.clear_news_events();
                sim.clear_audio_events();
                ctl.on_tick(sim);
                const GateGoal& goal = bot->gate_goals().goal();
                if (!placed && bot->gate_goals().attempts() > 0 && goal.kind == GoalKind::Hop) {      // an idle worker on the tile of the bomb and one on the tile to step from
                    sim.spawn_unit(0, sim::AntType::Worker, goal.hop.bomb);
                    sim.spawn_unit(0, sim::AntType::Worker, goal.hop.from);
                    placed = true;
                }
                if (first_bank == 0 && sim.get_player_score(0) > 0) {
                    first_bank = t + 1;
                    given_up_then = bot->gate_goals().given_up();
                }
            }
            ASSERT_TRUE(placed);
            ASSERT_TRUE(first_bank > 0);                                               // the food was banked ...
            ASSERT_TRUE(first_bank < 3000);                                            // ... in the first attempt's time (an attempt that waits for a step gives up after 1,800 ticks)
            ASSERT_EQ(given_up_then, 0u);                                              // ... with no attempt given up first
        }
    } TEST_END();
    TEST_CASE("AI27.10 Which Maps Are Played As Gate Maps: HELLRAISER And POPcOrN At Medium And Hard, Not At Easy; None Of The Six Shipped Maps (ISLANDS Is Left To The Island Play), Not Bombz Away") {
        struct Case {
            std::string path;
            bool gated;                                                                // for Medium and Hard
            bool analysed_walled;                                                      // GateInfo::walled for every seat
        };
        const std::string orig = ORIGINAL_ASSETS_DIR;
        const std::string comm = std::string(ANTS_SOURCE_DIR) + "/Community-Maps/";
        const std::vector<Case> cases = {
            {comm + "HELLRAISER.lvl", true, true},        {comm + "POPcOrN.lvl", true, true},         {comm + "Bombz Away.lvl", false, false},
            {orig + "/Maps/TINY.LVL", false, false},      {orig + "/Maps/SMALL.LVL", false, false},    {orig + "/Maps/MEDIUM.LVL", false, false},
            {orig + "/Maps/GAUNTLET.LVL", false, false},  {orig + "/Maps/TREASURE.LVL", false, false}, {orig + "/Maps/ISLANDS.LVL", false, true},
        };
        for (const Case& c : cases) {
            assets::LevelData level;
            ASSERT_TRUE(level.load_from_file(c.path));
            {
                sim::SimulationEngine sim;
                sim.init(level, 1, 0x0F);
                const MapInfo map(sim);
                for (uint8_t team = 0; team < 4; ++team) ASSERT_EQ(GateInfo::analyse(map, sim.grid(), team).walled(), c.analysed_walled);
            }
            for (const Level lvl : {Level::Easy, Level::Medium, Level::Hard}) {
                ArenaSpec spec;
                spec.level = &level;
                spec.seed = 1;
                spec.max_ticks = 400;
                for (uint8_t seat = 0; seat < 4; ++seat) {
                    BotSpec b;
                    b.seat = seat;
                    b.kind = "standard";
                    b.level = lvl;
                    spec.bots.push_back(b);
                }
                std::vector<StandardBot*> bots;
                spec.factory = [&bots](const BotSpec& b) -> std::unique_ptr<Bot> {
                    auto bot = std::make_unique<StandardBot>(b.level, b.style);
                    bots.push_back(bot.get());
                    return bot;
                };
                int gated = 0;
                spec.inspect = [&bots, &gated](const sim::SimulationEngine&) {
                    for (const StandardBot* bot : bots) gated += bot->gate_map() ? 1 : 0;
                };
                const ArenaResult r = play_match(spec);
                ASSERT_TRUE(r.error.empty());
                ASSERT_EQ(bots.size(), 4u);
                ASSERT_EQ(gated, (c.gated && lvl != Level::Easy) ? 4 : 0);
            }
        }
    } TEST_END();
    TEST_CASE("AI27.11 An Open Map Is Played As It Was: The Decision Is Taken Once, At The First Look (Fire Walls Lit Later Do Not Make A Gate Map), And The Pick-Up Trips Keep Their Old Rules (Three Candidates, The Contest, The Map Bombs)") {
        {   // (a) the decision is not taken again: a free pile at the first look, a fire wall across the world 300 ticks later
            Field f;
            f.powerup(8, 8, sim::AntType::Fire);
            Rig rig(f.sim, 0, Level::Hard, std::make_unique<StandardBot>(goal_plan()));
            auto& bot = rig.as<StandardBot>();
            rig.run(300);
            ASSERT_FALSE(bot.gate_map());
            f.fire_wall();                                                             // the pile is behind fire now
            rig.run(1500);
            ASSERT_FALSE(bot.gate_map());
            ASSERT_EQ(bot.gate_goals().attempts(), 0u);
        }
        {   // (a2) a pile that the hill reaches at the first look, and a wall round the other: when the free pile is eaten the bot still plays the open map (no goal is ever planned)
            Field f;
            f.rock_wall();
            f.powerup(8, 8, sim::AntType::Bomber);
            f.powerup(30, 30, sim::AntType::Swimmer);
            place_crackers(f.sim, 30, 12, 25);
            Rig rig(f.sim, 0, Level::Hard, std::make_unique<StandardBot>(goal_plan()));
            auto& bot = rig.as<StandardBot>();
            rig.run(4000);
            ASSERT_FALSE(bot.gate_map());
            ASSERT_TRUE(f.sim.get_player_score(0) >= 100);                             // the free pile was eaten (4 units of 25)
            ASSERT_EQ(bot.gate_goals().attempts(), 0u);
        }
        {   // (b) the candidate limit: three Fire power-ups walled in by rocks (the three nearest) and a fourth in the open. The old trip looks at three candidates and starts none
            Field f;
            for (const int cx : {8, 12, 16}) {
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx != 0 || dy != 0) f.sim.set_terrain(cx + dx, 16 + dy, sim::TERRAIN_OBSTACLE);
                    }
                }
                f.powerup(cx, 16, sim::AntType::Fire);
            }
            f.powerup(22, 10, sim::AntType::Fire);
            Rig rig(f.sim, 0, Level::Hard, std::make_unique<StandardBot>(goal_plan()));
            auto& bot = rig.as<StandardBot>();
            rig.run(600);
            ASSERT_FALSE(bot.gate_map());
            ASSERT_EQ(bot.powerups().taken(), 0u);
        }
        {   // (c) the contest: an enemy ant two tiles from the Fire power-up gets there first, so the trip is given up. On an open map at every level, and on a gate map at Hard (the race is judged there as everywhere);
            // Medium keeps the skip of a gate map and starts the trip: its longer reaction delay loses the estimate to the start ants of the enemy at the central power-ups (Ghetto: about a quarter of its score)
            struct Case {
                bool gate;
                Level level;
            };
            for (const Case c : {Case{false, Level::Hard}, Case{false, Level::Medium}, Case{true, Level::Hard}, Case{true, Level::Medium}}) {
                Field f;
                if (c.gate) {
                    f.rock_wall();                                                     // the pile is walled off: a gate map ...
                    f.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{12, 45});      // ... with a worker beside it, so the opening is not held back (AI27.13c)
                }
                f.powerup(22, 10, sim::AntType::Fire);
                f.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{24, 10});
                Rig rig(f.sim, 0, c.level, std::make_unique<StandardBot>(goal_plan(c.level)));
                auto& bot = rig.as<StandardBot>();
                rig.run(120);
                ASSERT_EQ(bot.gate_map(), c.gate);
                const bool skipped = c.gate && c.level == Level::Medium;               // the one case that does not give the trip up
                ASSERT_EQ(bot.powerups().contested() >= 1u, !skipped);
                ASSERT_EQ(bot.powerups().started() >= 1u, skipped);
                if (!skipped) ASSERT_EQ(bot.powerups().taken(), 0u);
            }
        }
        {   // (c2) the power-up on the hill's side decides the skip by itself: Medium's only free worker stands beyond a fire wall of the start (another component of the start's analysis) when
            // a cell of the wall burns out, and the Fire power-up on the hill's side with an enemy ant as near is still a trip (the component of the ant alone would not skip the contest)
            sim::SimulationEngine sim;
            empty_field(sim, 3, 14400);
            place_crackers(sim, 12, 40, 25);
            for (int x = 0; x < 60; ++x) sim.set_terrain(x, 20, sim::TERRAIN_OBSTACLE);    // the pile is walled off: a gate map ...
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{12, 45});                    // ... with a worker beside the pile (the opening is not held back: AI27.13c)
            for (int x = 0; x < 60; ++x) sim.set_fire_at(TileCoord{x, 14}, x == 12 ? 40u : 1000000u);   // a fire wall across the hill's side; the cell (12, 14) goes out after 40 ticks
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{12, 16});                    // the only worker that can walk to the power-up, once the cell is out
            sim.grid_mut().place_powerup(22, 10, static_cast<uint8_t>(sim::AntType::Fire));
            sim.spawn_unit(1, sim::AntType::Worker, TileCoord{24, 10});                    // an enemy ant two tiles from it
            Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(goal_plan(Level::Medium)));
            auto& bot = rig.as<StandardBot>();
            rig.run(400);
            ASSERT_TRUE(bot.gate_map());
            ASSERT_FALSE(sim.has_fire_at(TileCoord{12, 14}));
            ASSERT_TRUE(bot.powerups().started() >= 1u);                               // the trip is made: the power-up lies on the hill's side
        }
        {   // (d) the map bombs: the nearest Fire power-up lies in a box whose only doorway is plugged by a map bomb. A trip on an open map walks into the bomb (a gate map routes round it: AI27.7)
            Field f;
            for (int y = 6; y <= 10; ++y) {
                for (int x = 15; x <= 19; ++x) {
                    const bool border = x == 15 || x == 19 || y == 6 || y == 10;
                    if (border && !(x == 15 && y == 8)) f.sim.set_terrain(x, y, sim::TERRAIN_OBSTACLE);
                }
            }
            f.sim.grid_mut().place_bomb(15, 8, 255);
            f.powerup(17, 8, sim::AntType::Fire);
            f.powerup(12, 16, sim::AntType::Fire);
            f.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{14, 8});
            Rig rig(f.sim, 0, Level::Hard, std::make_unique<StandardBot>(goal_plan()));
            auto& bot = rig.as<StandardBot>();
            rig.run(250);
            ASSERT_FALSE(bot.gate_map());
            ASSERT_FALSE(f.sim.grid().has_bomb_at(TileCoord{15, 8}));
        }
        {   // (d2) a pile beyond a lake: no pile that the hill reaches on foot, but the island play owns this map, so it is no gate map and the bot opens as it did (it takes the Fire power-up of its side)
            Field f;
            for (int y = 20; y <= 22; ++y) {
                for (int x = 0; x < 60; ++x) f.sim.set_terrain(x, y, sim::TERRAIN_WATER);
            }
            f.powerup(8, 8, sim::AntType::Fire);
            Rig rig(f.sim, 0, Level::Hard, std::make_unique<StandardBot>(goal_plan()));
            auto& bot = rig.as<StandardBot>();
            rig.run(600);
            ASSERT_TRUE(bot.islands().active());
            ASSERT_FALSE(bot.gate_map());
            ASSERT_TRUE(bot.powerups().taken() >= 1u);
        }
        {   // (d3) a pile in a ring of power-ups: the analysis calls it walled off, but the plain economy takes the power-ups and so opens the ring itself (Running Ants): not a gate map
            sim::SimulationEngine sim;
            empty_field(sim, 3, 14400);
            for (int k = 0; k < 8; ++k) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{6 + k, 10});
            place_crackers(sim, 30, 30, 25);
            for (int y = 28; y <= 33; ++y) {
                for (int x = 28; x <= 33; ++x) {
                    if (x == 28 || x == 33 || y == 28 || y == 33) sim.grid_mut().place_powerup(x, y, static_cast<uint8_t>(sim::AntType::Swimmer));
                }
            }
            ASSERT_TRUE(GateInfo::analyse(MapInfo(sim), sim.grid(), 0).walled());
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(goal_plan()));
            rig.run(200);
            ASSERT_FALSE(rig.as<StandardBot>().gate_map());
        }
        {   // (f) the ferry: an idle Swimmer on an open world (a pile the hill reaches on foot) is nobody's ferry job off a gate map and off the island play; the economy keeps it
            Field f;
            f.sim.spawn_unit(0, sim::AntType::Swimmer, TileCoord{10, 12});
            Rig rig(f.sim, 0, Level::Hard, std::make_unique<StandardBot>(goal_plan()));
            auto& bot = rig.as<StandardBot>();
            size_t claimed = 0;
            for (int look = 0; look < 30; ++look) {
                rig.run(20);
                claimed = std::max(claimed, bot.ledger().count(StandardBot::kFerry));
            }
            ASSERT_FALSE(bot.gate_map());
            ASSERT_FALSE(bot.islands().active());
            ASSERT_EQ(claimed, 0u);
        }
        {   // (e) a world with no pile at all has no pile that is walled off: not a gate map
            sim::SimulationEngine sim;
            empty_field(sim, 3, 14400);
            for (int k = 0; k < 8; ++k) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{6 + k, 10});
            sim.grid_mut().place_powerup(8, 8, static_cast<uint8_t>(sim::AntType::Fire));
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(goal_plan()));
            rig.run(300);
            ASSERT_FALSE(rig.as<StandardBot>().gate_map());
        }
    } TEST_END();

    TEST_CASE("AI27.12 The Plant Tile And The Tile To Step From Are Cleared Of Own Ants: An Idle Worker On Each Is Sent Aside, The Hop Is Done, And No Ant Stays Held") {
        Field f;
        f.rock_wall();
        f.powerup(8, 8, sim::AntType::Bomber);
        f.powerup(30, 30, sim::AntType::Swimmer);
        const GateGoal g = f.goal();
        ASSERT_EQ(g.kind, GoalKind::Hop);
        const uint32_t on_bomb = f.sim.spawn_unit(0, sim::AntType::Worker, g.hop.bomb);
        f.sim.spawn_unit(0, sim::AntType::Worker, g.hop.from);
        Rig rig(f.sim, 0, Level::Hard, std::make_unique<StandardBot>(goal_plan()));
        auto& bot = rig.as<StandardBot>();
        size_t held = 0;
        for (int look = 0; look < 120; ++look) {
            rig.run(20);
            held = std::max(held, bot.ledger().count(StandardBot::kGateGoals));
        }
        ASSERT_TRUE(held >= 2u);                                                       // the Bomber and an ant that was sent aside were held at once
        ASSERT_EQ(bot.gate_goals().done(), 1u);                                        // the hop was done, though an ant stood on the tile of the bomb and on the tile in front of it
        ASSERT_TRUE(bot.gate_goals().plants() >= 1u);
        ASSERT_TRUE(f.sim.get_unit(on_bomb).pixel_y / 32 != g.hop.bomb.y || f.sim.get_unit(on_bomb).pixel_x / 32 != g.hop.bomb.x);
        ASSERT_EQ(bot.ledger().count(StandardBot::kGateGoals), 0u);                    // and the ants that were sent aside are free again
    } TEST_END();
    TEST_CASE("AI27.12b An Enemy Ant On The Tile To Step From Is Waited For, Not Attacked: A Plain Move Onto An Enemy Ant's Tile Is An Attack In The Engine, So The Bomber Stays Off It And The Enemy Is Unhurt") {
        Field f;
        f.rock_wall();
        f.powerup(8, 8, sim::AntType::Bomber);
        f.powerup(30, 30, sim::AntType::Swimmer);
        const GateGoal g = f.goal();
        ASSERT_EQ(g.kind, GoalKind::Hop);
        const uint32_t enemy = f.sim.spawn_unit(1, sim::AntType::Worker, g.hop.from);
        Rig rig(f.sim, 0, Level::Hard, std::make_unique<StandardBot>(goal_plan()));
        auto& bot = rig.as<StandardBot>();
        for (int look = 0; look < 60; ++look) rig.run(20);                             // 1,200 ticks: the Bomber is fetched, plants and wants to step; the step of the task gives up only after 1,800
        ASSERT_TRUE(bot.gate_goals().plants() >= 1u);                                  // the Bomber got as far as the plant ...
        ASSERT_EQ(f.sim.get_unit(enemy).hp, 10);                                       // ... and the ant on the tile to step from was not touched
        ASSERT_EQ(f.sim.get_unit(enemy).pixel_x / 32, g.hop.from.x);
        ASSERT_EQ(f.sim.get_unit(enemy).pixel_y / 32, g.hop.from.y);
        ASSERT_EQ(bot.gate_goals().done(), 0u);
    } TEST_END();
    TEST_CASE("AI27.13 The Opening Of A Gate Map: The Trip To The Fire Power-Up Of The Own Side Is Held Back While Nothing Is Banked, For 2,400 Ticks, And Made After Them") {
        Field f;
        f.rock_wall();                                                                 // a gate map with no Bomber and no Swimmer anywhere: no goal can be planned
        f.powerup(8, 8, sim::AntType::Fire);
        Rig rig(f.sim, 0, Level::Hard, std::make_unique<StandardBot>(goal_plan()));
        auto& bot = rig.as<StandardBot>();
        rig.run(2300);
        ASSERT_TRUE(bot.gate_map());
        ASSERT_EQ(bot.powerups().taken(), 0u);                                         // held back: the Fire Ant of the own side is not secured
        rig.run(1700);
        ASSERT_TRUE(bot.powerups().taken() >= 1u);                                     // and secured when the opening is over
    } TEST_END();
    TEST_CASE("AI27.13c A Worker Beside A Pile At The First Look: The Flag Is Set And The Opening Is Not Held Back (The Same World With The Worker On The Hill's Side, Or With Another Kind Of Ant Beside The Pile, Holds The Trip To The Fire Power-Up Back)") {
        enum class Beside { Nobody, WorkerAtHill, BomberAtPile, WorkerAtPile };
        for (const Beside who : {Beside::Nobody, Beside::WorkerAtHill, Beside::BomberAtPile, Beside::WorkerAtPile}) {
            Field f;                                                                   // (eight workers of seat 0 stand near the hill, on the hill's side of the wall)
            f.rock_wall();                                                             // a gate map with no Bomber power-up and no Swimmer anywhere: no goal can be planned
            f.powerup(8, 8, sim::AntType::Fire);
            if (who == Beside::WorkerAtHill) f.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{20, 12});
            if (who == Beside::BomberAtPile) f.sim.spawn_unit(0, sim::AntType::Bomber, TileCoord{12, 45});      // on the pile's side of the wall, beside the pile at (12, 40), but no harvester
            if (who == Beside::WorkerAtPile) f.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{12, 45});
            Rig rig(f.sim, 0, Level::Hard, std::make_unique<StandardBot>(goal_plan()));
            auto& bot = rig.as<StandardBot>();
            rig.run(300);
            const bool beside = who == Beside::WorkerAtPile;
            ASSERT_TRUE(bot.gate_map());                                               // (the pile is still walled off from the hill)
            ASSERT_EQ(bot.tactics().gate_beside, beside);
            rig.run(2000);
            ASSERT_EQ(bot.powerups().taken() >= 1u, beside);                           // the trip is made at once with the worker at the food, held back without it
        }
    } TEST_END();
    TEST_CASE("AI27.13b The Opening Of A Gate Map Puts No Fire Ant On Wall Duty: A Thief Power-Up That The Enemy Reaches Calls For Walls On An Open Map, Not On A Gate Map Before It Has Banked") {
        for (const bool gate : {false, true}) {
            Field f;
            if (gate) f.rock_wall();                                                   // the pile of the Field is walled in, or free
            f.powerup(40, 40, sim::AntType::Thief);                                    // the enemy hills reach it: a thief threatens
            Rig rig(f.sim, 0, Level::Hard, std::make_unique<StandardBot>(goal_plan()));
            auto& bot = rig.as<StandardBot>();
            bool demand = false;
            for (int k = 0; k < 20; ++k) {
                rig.run(30);
                demand = demand || bot.tactics().wall_demand;
            }
            ASSERT_EQ(bot.gate_map(), gate);
            ASSERT_EQ(demand, !gate);                                                  // (the open map asks for walls; the opening of a gate map asks for none)
        }
    } TEST_END();
    TEST_CASE("AI27.14 On A Gate Map The Counters Leave The Map's Own Bombs (Of No Team) Alone: They Are The Gate; A Team's Bomb There, And A Map Bomb On An Open Map, Are Countered As Before, And So Is A Map Bomb Once The Gate Goals Are Exhausted (Every Seat, Straight And Transposed)") {
        enum class Bomb : uint8_t { Map, Enemy };
        struct Case {
            bool gate;
            Bomb bomb;
            bool countered;
            bool exhausted{false};                                                 // the gate goals are out of attempts: nobody defuses the map's bombs but the counters
        };
        for (const Case c : {Case{true, Bomb::Map, false}, Case{true, Bomb::Enemy, true}, Case{false, Bomb::Map, true}, Case{true, Bomb::Map, true, true}}) {
            for (uint8_t d = 0; d < 8; ++d) {
                const Drawing w{static_cast<uint8_t>(d >> 1), (d & 1) != 0};
                sim::SimulationEngine sim;
                empty_field(sim, 3, 14400);
                const TileCoord pile = w.at(12, 40);
                place_crackers(sim, pile.x, pile.y, 25);
                if (c.gate) {
                    for (int y = 18; y <= 26; ++y) {
                        for (const TileCoord r : w.row(y)) sim.set_terrain(r.x, r.y, sim::TERRAIN_OBSTACLE);   // a band of rock no throw crosses: the pile is walled off, and there is no goal
                    }
                }
                const TileCoord bomb = w.at(9, 9);                                     // five tiles from the hill: in the way of the hill's own ants
                const uint8_t owner = c.bomb == Bomb::Map ? uint8_t{255} : static_cast<uint8_t>((w.seat + 1) % 4);
                sim.grid_mut().place_bomb(static_cast<uint32_t>(bomb.x), static_cast<uint32_t>(bomb.y), owner);
                sim.spawn_unit(w.seat, sim::AntType::Bomber, w.at(6, 10));
                sim.spawn_unit(w.seat, sim::AntType::Worker, w.at(7, 11));
                Rig rig(sim, w.seat, Level::Hard, std::make_unique<StandardBot>(goal_plan()));
                auto& bot = rig.as<StandardBot>();
                if (c.exhausted) {
                    GateGoalTask::Params gp = bot.gate_goals().params();
                    gp.goals_max = 0;                                              // (the task is exhausted at its first look)
                    bot.gate_goals_for_labs().set_params(gp);
                }
                rig.run(600);
                ASSERT_EQ(bot.gate_map(), c.gate);
                ASSERT_EQ(bot.gate_goals().exhausted(), c.exhausted);
                const bool acted = bot.bombs().defused() + bot.bombs().set_off() > 0 || bot.bombs().busy() || !sim.grid().has_bomb_at(bomb);
                ASSERT_EQ(acted, c.countered);
            }
        }
    } TEST_END();
}
