// AI27: the gate goals (docs/reverse_engineering/bomb_gates.md, "Gate goals"): what a Hard bot whose food is walled off by fire walls or bombs sets out to do (plan_gate_goal), and how the
// task works it (GateGoalTask): the right ant first, then the wall put out or the Bomber's hop, with the owner's times (a step 90 s, an attempt 4 minutes, two retries). Off in every plan.
//
//   AI27.1   the planner on hand-made worlds: no goal where food is free, a wall to put out, a hop onto a wanted power-up, a hop in reach of a pile, the rules that hold a goal back
//   AI27.2   the planner on POPcOrN: the same goal for every seat of a side, a hop of two bombs onto the Swimmer at (1,15)
//   AI27.3b  an ant of the kind on the far side of the wall is no ant for the goal
//   AI27.3   a Hard bot puts a fire wall out with a Fire ant and banks the food behind it; the same bot without the flag banks nothing
//   AI27.4   a Hard bot bombs itself over a rock wall onto a Swimmer power-up, also when a bomb is a dud, and takes it
//   AI27.4b  a chain of two bombs that stops at a dud: the attempt is given up as stranded
//   AI27.5   the times: a goal that cannot be served is given up after a step of 90 s, three attempts at most, then the bot plays on and asks for nothing
//   AI27.6   who plays it: not Medium, not Easy, not a plan without the flag; the numbers of the plan are the owner's
#include "ai_test.hpp"
#include "b41_helpers.hpp"
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
    LevelPlan plan = peace_plan(level);
    plan.gate_goals = true;
    return plan;
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
            ASSERT_TRUE(g.same(g) && !g.same(GateGoal{}));
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
        ASSERT_TRUE(goals[0].cell == (TileCoord{9, 7}) && goals[0].same(goals[2]));       // the two north seats, the two south seats: the same tile, mirrored (rows 7 and 23)
        ASSERT_TRUE(goals[1].cell == (TileCoord{9, 23}) && goals[1].same(goals[3]));
        ASSERT_TRUE(goals[0].hop.lands.size() == 2 && goals[0].hop.lands[0] == (TileCoord{5, 11}));
    } TEST_END();

    TEST_CASE("AI27.3 A Hard Bot With The Goal On Puts A Fire Wall Out With a Fire Ant And Banks The Food Behind It; The Same Bot Without The Flag Banks Nothing") {
        for (int on = 0; on < 2; ++on) {
            Field f;
            f.fire_wall();
            f.powerup(8, 8, sim::AntType::Fire);
            LevelPlan plan = peace_plan(Level::Hard);
            plan.gate_goals = on != 0;
            Rig rig(f.sim, 0, Level::Hard, std::make_unique<StandardBot>(plan));
            auto& bot = rig.as<StandardBot>();
            rig.run(3000);
            if (on == 0) {
                ASSERT_EQ(f.sim.get_player_score(0), 0);
                ASSERT_EQ(bot.gate_goals().attempts(), 0u);
                ASSERT_EQ(fires_in_row(f.sim, 20), 60);
                continue;
            }
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
            rig.run(1200);
            done += bot.gate_goals().done();
            if (std::string(bot.gate_goals().last_failure()) == "stranded") {
                ++stranded;
                ASSERT_TRUE(bot.gate_goals().given_up() >= 1u);
                ASSERT_EQ(bot.ledger().count(StandardBot::kGateGoals) <= 1u, true);
            }
        }
        ASSERT_TRUE(stranded >= 1u);                                                    // a dud on the second bomb (a fifth of the throws) happened in these seeds
        ASSERT_TRUE(done >= 1u);                                                        // and a chain that flew on
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
        rig.run(400);
        ASSERT_EQ(bot.gate_goals().given_up(), 1u);                                    // after the step: the first attempt is over
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

    TEST_CASE("AI27.6 Who Plays It: Not Medium, Not Easy, Not A Plan Without The Flag; The Plans Have It Off, The Numbers Are The Owner's") {
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            const LevelPlan plan = plan_for(level);
            ASSERT_FALSE(plan.gate_goals);
            ASSERT_EQ(plan.gate_step_ticks, 1800u);
            ASSERT_EQ(plan.gate_attempt_ticks, 4800u);
            ASSERT_EQ(plan.gate_retries, 2u);
        }
        for (const Level level : {Level::Easy, Level::Medium}) {
            Field f;
            f.fire_wall();
            f.powerup(8, 8, sim::AntType::Fire);
            LevelPlan plan = goal_plan(level);                                         // the flag on, below Hard
            Rig rig(f.sim, 0, level, std::make_unique<StandardBot>(plan));
            rig.run(1500);
            ASSERT_EQ(rig.as<StandardBot>().gate_goals().attempts(), 0u);
            ASSERT_EQ(fires_in_row(f.sim, 20), 60);
        }
    } TEST_END();
}
