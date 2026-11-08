// The heal batch (docs/BOT_PEOPLE.md, "Step 2: hurt ants heal"; include/ants_ai/heal_task.hpp): a hurt ant of a bot that has the task turned on walks into its hill when it is in danger, which restores its hit points, and
// goes back to its job. The task is built and off in the shipped plan of every level, so the cases turn it on with the level's numbers (hard_plan(), with_heal); in the game type 187 (a closed hill) it does nothing. Hand-made worlds (b41_helpers.hpp), quick enough for suite 2.20. Two kinds of
// case: the bot as a whole (a StandardBot with the plan that ships and the task turned on, through the rig: every other task plays, reach_fights and the kill batch included) and the task by hand (a HealTask that is looked at with the
// real view and the real map, and the ledger and ranks of a standard bot), for the edges of each rule.
//
//   AI36.1   a Hard worker with 4 hit points and an enemy worker 2 tiles away, 8 tiles from home, enters its hill and has 10 hit points within 300 ticks (the task turned on); without it it does not go
//   AI36.2   the same ant with no enemy near: at 4 hit points, 8 tiles out, it does not go; at 2 hit points and 5 tiles out it goes, and at 2 hit points and 8 tiles out it does not
//   AI36.3   who does not go: a Thief of the raid task near the raid tile of an enemy hill, the keeper of the walls while an enemy Thief stands within 4 tiles of the raid tile, an ant 20 tiles from home
//   AI36.4   a carrier with 3 hit points that a blow hit banks its food and heals, and one order is made for it at a look, not two (the gate task and the carrier aid leave it to the task)
//   AI36.5   the game type 187 has a closed hill: nothing is ordered (with or without the brawl); in a 187 match of the earlier rules the hill is open and the task works
//   AI36.6   the task is off in the shipped plan of every level and the shipped bots play as before: the same counters and state hash as the plan without the task whatever the numbers of the batch say; with it on they do not
//   AI36.7   the numbers of who is sent (heal_wanted): the hit points, the danger, the blow within the window, the calm top-up and its 6 tiles, the walk limit; the walk home in tiles; the enemy ants near
//   AI36.8   the task by hand, who is taken: the hit points, the danger radius, the window of a blow, the top-up, the carriers, the ants that are held by tasks of each rank, the ones that are never taken
//   AI36.9   the task by hand, who is not taken: a Thief of the raid task about to raid, the keeper of the walls, the island play, an ant that stands on a power-up, on the mound, in its hit clip, a hill
//            that is closed or not there, a ring of fire walls round the gate
//   AI36.10  the order: one group move for every ant that is due, Urgent, onto the entrance; an ant that walks nearer is not ordered again, one that stands idle or walks away is after the gap, at most
//            8 times; after 600 ticks the ant is given back and left alone for 600 more; hit points back to 10 or gone: given back
//   AI36.11  the fights and the other tasks: a fight never takes an ant that the task holds, the task takes the ant that a fight lets go, and the unjam of the ramp leaves a healing ant where it is
#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include "ai_test.hpp"
#include "ants_ai/defence.hpp"
#include "ants_ai/heal_task.hpp"
#include "ants_ai/standard_bot.hpp"
#include "b41_helpers.hpp"
#include "contest_helpers.hpp"

using namespace ai_test;
using namespace ants;
using namespace ants::ai;
using namespace b41;
using namespace contest;

namespace {

constexpr TileCoord kEntrance{5, 5};                       // the entrance of the hill of seat 0 (its hill is at (4, 4)); the three tiles of its queue row are (4..6, 3), cost 0: the tile (6 + k, 3) is k tiles from home
constexpr uint64_t kGap = 40;                              // the gap between two orders to one ant

// The plan of a level with the task turned on (it is off in every shipped plan): the numbers are the level's own
LevelPlan with_heal(LevelPlan plan) {
    plan.heal = true;
    return plan;
}
LevelPlan hard_plan() { return with_heal(plan_for(Level::Hard)); }

// A hurt ant of seat 0
uint32_t hurt(sim::SimulationEngine& sim, sim::AntType type, int32_t x, int32_t y, unsigned hp) {
    const uint32_t id = sim.spawn_unit(0, type, TileCoord{x, y});
    sim.get_unit(id).hp = static_cast<uint8_t>(hp);
    return id;
}

// An enemy ant (seat 1) that stands where it is
uint32_t foe(sim::SimulationEngine& sim, int32_t x, int32_t y, sim::AntType type = sim::AntType::Worker) { return sim.spawn_unit(1, type, TileCoord{x, y}); }

bool names(const Command& c, uint32_t ant) { return std::find(c.ants.begin(), c.ants.end(), ant) != c.ants.end(); }

// The group moves onto the entrance in a set of orders
std::vector<const Intent*> homeward(const Orders& orders) {
    std::vector<const Intent*> out;
    for (const Intent& in : orders.intents()) {
        if (in.command.type == CommandType::GroupMove && in.command.tile_x == kEntrance.x && in.command.tile_y == kEntrance.y) out.push_back(&in);
    }
    return out;
}

bool sent_home(const Orders& orders, uint32_t ant) {
    for (const Intent* in : homeward(orders)) {
        if (names(in->command, ant)) return true;
    }
    return false;
}

// ---- the task by hand ------------------------------------------------------------------------------------------------------------------------------------------

// A HealTask that is looked at by hand: the real view of the engine and the real map, a Tactics of its own (the test sets the keeper of the walls), and the ledger of a standard bot that has started (its ranks)
class Harness {
public:
    sim::SimulationEngine sim;
    LevelPlan plan = hard_plan();
    Tactics tactics;
    std::unique_ptr<MapInfo> map;
    std::unique_ptr<HealTask> task;

    // The world must be built first; 10 ticks go by so that no tick is 0
    void begin() {
        map = std::make_unique<MapInfo>(sim);
        tactics.plan = plan;
        probe_ = std::make_unique<StandardBot>(plan);
        probe_->start(BotContext{0, profile_for(plan.level), 1, map.get()});
        task = std::make_unique<HealTask>(StandardBot::kHeal, tactics);
        tick_all(sim, 10);
    }
    AntLedger& ledger() { return probe_->ledger_for_labs(); }
    // One look at the tick that the engine is at: the memory is updated, the task steps (and `after`, a task of the bot that steps after it, if given); what they ordered
    Orders look(Task* after = nullptr) {
        const BotView view = BotView::build(sim, 0, map.get());
        tactics.memory.update(view, *map);
        Orders orders;
        const Profile profile = profile_for(plan.level);
        TaskContext context{view, orders, ledger(), profile, *map, 0};
        ledger().forget_missing(view.mine());
        task->step(context);
        if (after != nullptr) after->step(context);
        return orders;
    }
    void tick(uint64_t n) { tick_all(sim, n); }
    uint64_t now() const { return sim.current_tick(); }

private:
    std::unique_ptr<StandardBot> probe_;
};

// A world of the four hills with the given plan: the ants are put, then begin() makes the map and the task; every look is the test's
struct World {
    Harness h;
    explicit World(const LevelPlan& plan = hard_plan(), sim::GameMode mode = sim::GameMode::HighestScore, bool open_hill = false) {
        h.plan = plan;
        h.sim.set_game_mode(mode);
        if (open_hill) h.sim.set_hill_open_187(true);
        empty_field(h.sim, 36);
    }
    void begin() { h.begin(); }
};

// What happened to one hurt ant at the first look: it was ordered home, the task holds it
struct Went {
    bool sent;
    bool held;
};

// A hurt ant at (x, y) and, `gap` tiles east of it on the same row, an enemy ant of `foe_type` (gap < 0: none); the first look
Went first_look(const LevelPlan& plan, sim::AntType type, unsigned hp, int32_t x, int32_t y, int32_t gap, sim::AntType foe_type = sim::AntType::Worker) {
    World w(plan);
    const uint32_t ant = hurt(w.h.sim, type, x, y, hp);
    if (gap >= 0) foe(w.h.sim, x + gap, y, foe_type);
    w.begin();
    const Orders orders = w.h.look();
    return Went{sent_home(orders, ant), w.h.ledger().owner(ant) == StandardBot::kHeal};
}

// ---- the bot as a whole ----------------------------------------------------------------------------------------------------------------------------------------

// The plan that ships at a level, as a bot plays it (no style, no variations)
LevelPlan shipped(Level level, bool heal) {
    LevelPlan p = plan_for(level);
    if (heal) p.heal = true;
    else without_heal(p);
    return p;
}

}  // namespace

void run_heal_tests() {
    TEST_CASE("AI36.1 A Hard Worker With 4 Hit Points And An Enemy Worker 2 Tiles Away, 8 Tiles From Home, Walks Into Its Hill And Has 10 Hit Points Within 300 Ticks (The Task Turned On, Every Other Task Playing); Without The Task It Does Not Go")
    {
        for (const bool heal : {true, false}) {
            sim::SimulationEngine sim;
            empty_field(sim, 361);
            add_pile(sim, 30, 30, 40, 25);                                                                   // (what the worker would do instead: a pile far from the hill)
            const uint32_t ant = hurt(sim, sim::AntType::Worker, 13, 5, 4);                                  // 8 tiles of walk from the entrance
            foe(sim, 15, 5);
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{44, 44});                                      // (two more ants: the bot does not keep to one)
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{45, 44});
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(shipped(Level::Hard, heal)), 4, 8);
            ASSERT_EQ(walk_home_tiles(rig.map(), 0, TileCoord{13, 5}), 8);
            uint64_t full_at = 0;
            bool held = false;
            for (uint64_t t = 0; t < 300; ++t) {
                rig.tick();
                const sim::AntSnapshot* a = snapshot_of(sim, ant);
                ASSERT_TRUE(a != nullptr);
                held = held || rig.as<StandardBot>().ledger().owner(ant) == StandardBot::kHeal;
                if (full_at == 0 && a->hp >= 10) full_at = t + 1;
            }
            if (heal) {
                ASSERT_TRUE(full_at != 0 && full_at <= 300);
                ASSERT_TRUE(held);
                ASSERT_TRUE(alive(sim, ant));
                ASSERT_EQ(rig.as<StandardBot>().heal().healed(), 1u);
                ASSERT_TRUE(rig.as<StandardBot>().ledger().owner(ant) != StandardBot::kHeal);                  // (given back: its old task asks for it again)
            } else {
                ASSERT_EQ(full_at, 0u);
                ASSERT_FALSE(held);
                ASSERT_EQ(rig.as<StandardBot>().heal().started(), 0u);
            }
        }
    } TEST_END();

    TEST_CASE("AI36.2 The Same Ant With No Enemy Near: At 4 Hit Points, 8 Tiles Out, It Does Not Go (The Top-Up Is For 2 Hit Points); At 2 Hit Points And 5 Tiles Out It Goes And Is Healed, At 2 Hit Points And 8 Tiles Out It Does Not (The Top-Up Walk Is At Most 6 Tiles)")
    {
        struct Case {
            unsigned hp;
            int32_t x;
            int32_t walk;
            bool goes;
        };
        for (const Case k : {Case{4, 13, 8, false}, Case{2, 10, 5, true}, Case{2, 13, 8, false}, Case{3, 10, 5, false}}) {
            sim::SimulationEngine sim;
            empty_field(sim, 362);
            const uint32_t ant = hurt(sim, sim::AntType::Worker, k.x, 5, k.hp);
            foe(sim, 45, 45);                                                                                // (the enemy team plays, far away)
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{44, 44});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{45, 44});
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(hard_plan()), 4, 8);
            ASSERT_EQ(walk_home_tiles(rig.map(), 0, TileCoord{k.x, 5}), k.walk);
            rig.run(260);
            if (k.goes) {
                ASSERT_EQ(rig.as<StandardBot>().heal().started(), 1u);
                ASSERT_EQ(sim.get_unit(ant).hp, 10u);
            } else {
                ASSERT_EQ(rig.as<StandardBot>().heal().started(), 0u);
                ASSERT_EQ(sim.get_unit(ant).hp, static_cast<uint8_t>(k.hp));
            }
        }
    } TEST_END();

    TEST_CASE("AI36.3 Who Does Not Go: A Thief Of The Raid Task Near The Raid Tile Of An Enemy Hill, The Keeper Of The Walls While An Enemy Thief Stands 3 Tiles From The Raid Tile Of The Own Hill, An Ant 20 Tiles From Home; The Same Ants Go When The Reason Is Taken Away")
    {
        // (a) the Thief of the raid task about to raid (the walk limit is raised: the raid tile of the enemy hill is far from home), in the task by hand
        for (const bool raiding : {true, false}) {
            LevelPlan plan = hard_plan();
            plan.heal_walk_tiles = 400;
            World w(plan);
            const uint32_t thief = hurt(w.h.sim, sim::AntType::Thief, 50, 9, 4);                                 // 3 tiles from the raid tile of the hill of seat 1
            foe(w.h.sim, 51, 9);
            w.h.begin();
            if (raiding) ASSERT_TRUE(w.h.ledger().claim(thief, StandardBot::kRaids));
            const Orders o = w.h.look();
            ASSERT_EQ(sent_home(o, thief), !raiding);
            ASSERT_EQ(w.h.ledger().owner(thief) == StandardBot::kHeal, !raiding);
        }
        {   // in its raid clip it takes no order at all: nothing is ordered whatever holds it
            LevelPlan plan = hard_plan();
            plan.heal_walk_tiles = 400;
            World w(plan);
            w.h.begin();
            const uint32_t thief = hurt(w.h.sim, sim::AntType::Thief, 49, 8, 4);
            foe(w.h.sim, 49, 9);
            w.h.sim.start_thief_infiltration(thief, 1);
            const BotView view = BotView::build(w.h.sim, 0, w.h.map.get());
            for (const AntView& a : view.mine()) {
                if (a.id == thief) ASSERT_TRUE(a.state == sim::UnitState::Infiltrating);
            }
            ASSERT_FALSE(sent_home(w.h.look(), thief));
        }
        // (b) the keeper of the walls (the bot as a whole: a Fire Ant that the walls of a bot that sees a thief coming hold; it is chosen in the first looks, and then hurt): an enemy Thief 3 tiles from the
        // raid tile (7, 6) keeps it at its job, one far away does not
        for (const bool thief_near : {true, false}) {
            sim::SimulationEngine sim;
            empty_field(sim, 363);
            const uint32_t keeper = sim.spawn_unit(0, sim::AntType::Fire, TileCoord{10, 3});
            foe(sim, 11, 3);                                                                                 // (a striker next to it: the danger)
            foe(sim, thief_near ? 10 : 44, thief_near ? 6 : 8, sim::AntType::Thief);
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{44, 44});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{45, 44});
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(hard_plan()), 4, 8);
            rig.run(40);
            ASSERT_TRUE(rig.as<StandardBot>().tactics().wall_demand && rig.as<StandardBot>().tactics().wall_keeper == keeper);       // (the premise: it is the keeper while a thief threatens)
            sim.get_unit(keeper).hp = 4;
            bool kept = true;
            bool taken = false;
            for (int t = 0; t < 120; ++t) {
                rig.tick();
                const StandardBot& bot = rig.as<StandardBot>();
                kept = kept && bot.tactics().wall_keeper == keeper;
                taken = taken || bot.ledger().owner(keeper) == StandardBot::kHeal;
            }
            ASSERT_EQ(taken, !thief_near);
            ASSERT_EQ(rig.as<StandardBot>().heal().started() != 0, !thief_near);
        }
        // (c) an ant 20 tiles from home (walk 20) is not taken; 16 tiles (the limit) it is
        for (const int32_t x : {22, 26}) {
            sim::SimulationEngine sim;
            empty_field(sim, 364);
            const uint32_t ant = hurt(sim, sim::AntType::Worker, x, 3, 4);
            foe(sim, x + 1, 3);
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{44, 44});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{45, 44});
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(hard_plan()), 4, 8);
            ASSERT_EQ(walk_home_tiles(rig.map(), 0, TileCoord{x, 3}), x - 6);
            rig.run(40);
            ASSERT_EQ(rig.as<StandardBot>().heal().started() != 0, x == 22);
            ASSERT_EQ(rig.as<StandardBot>().ledger().owner(ant) == StandardBot::kHeal, x == 22);
        }
    } TEST_END();

    TEST_CASE("AI36.4 A Carrier With 4 Hit Points That A Blow Hits Walks Home, Banks Its Food And Is Healed, And One Order Is Made For It At A Look, Not Two: The Gate Task (Hard) And The Carrier Aid (The Plan Without The Gate) Leave It To The Task")
    {
        for (const bool gate : {true, false}) {
            sim::SimulationEngine sim;
            empty_field(sim, 365);
            const uint32_t carrier = carrier_at(sim, 0, TileCoord{13, 3});
            sim.get_unit(carrier).hp = 4;
            const uint32_t enemy = foe(sim, 14, 3);
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{44, 44});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{45, 44});
            LevelPlan plan = hard_plan();
            plan.gate = gate;
            ASSERT_TRUE(plan.carrier_aid);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 8);
            sim.apply_command(command_of(CommandType::GroupAttack, 1, {enemy}, 13, 3));                      // a blow on the carrier: 4 hit points -> 3, and its hit clip makes the first order wait
            rig.run(400);
            ASSERT_EQ(sim.get_player_score(0), 25);                                                          // (banked)
            ASSERT_EQ(sim.get_unit(carrier).hp, 10u);                                                        // (and healed)
            ASSERT_EQ(rig.as<StandardBot>().heal().started(), 1u);
            ASSERT_EQ(rig.as<StandardBot>().heal().healed(), 1u);
            std::map<uint64_t, size_t> per_look;
            for (const auto& e : rig.proposed) {
                if (e.second.type == CommandType::GroupMove && names(e.second, carrier)) ++per_look[e.first];
            }
            ASSERT_TRUE(!per_look.empty());
            for (const auto& l : per_look) ASSERT_EQ(l.second, 1u);                                          // (one order at a look)
            ASSERT_TRUE(per_look.size() <= 3u);                                                              // (the first, and what the blow's hit clip made it repeat)
            ASSERT_EQ(rig.as<StandardBot>().aid().sent_home(), 0u);                                          // (the carrier aid never clicked)
        }
    } TEST_END();

    TEST_CASE("AI36.5 The Game Type 187 Has A Closed Hill: Nothing Is Ordered (With The Brawl And Without It, The Task Itself Stays Away); In A 187 Match Of The Earlier Rules, With An Open Hill, The Task Works")
    {
        struct Case {
            bool brawl;
            bool open_hill;
            bool goes;
        };
        for (const Case k : {Case{true, false, false}, Case{false, false, false}, Case{false, true, true}}) {
            sim::SimulationEngine sim;
            sim.set_game_mode(sim::GameMode::Kills187);
            if (k.open_hill) sim.set_hill_open_187(true);
            empty_field(sim, 366);
            const uint32_t ant = hurt(sim, sim::AntType::Worker, 13, 5, 4);
            foe(sim, 15, 5);
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{44, 44});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{45, 44});
            LevelPlan plan = hard_plan();
            plan.brawl187 = k.brawl;
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 8);
            ASSERT_EQ(sim.own_hill_enterable(), k.open_hill);
            rig.run(120);
            ASSERT_EQ(rig.as<StandardBot>().heal().started() != 0, k.goes);
            ASSERT_EQ(rig.as<StandardBot>().heal().orders() != 0, k.goes);
            ASSERT_EQ(rig.as<StandardBot>().ledger().owner(ant) == StandardBot::kHeal, k.goes);
            size_t home_orders = 0;
            for (const auto& e : rig.proposed) home_orders += (e.second.type == CommandType::GroupMove && e.second.tile_x == kEntrance.x && e.second.tile_y == kEntrance.y && names(e.second, ant)) ? 1u : 0u;
            ASSERT_EQ(home_orders != 0, k.goes);
        }
    } TEST_END();

    TEST_CASE("AI36.6 The Task Is Off In The Shipped Plan Of Every Level, And The Shipped Bots Play As Before: The Same Counters, The Same Commands And The Same State Hash On A Fixed Scene (Hurt Ants Beside Enemy Workers) As The Plan Without The Task And Whatever The Numbers Of The Batch Say (Easy, Medium And Hard); With The Task Turned On Medium And Hard Do Not Play As Before")
    {
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            const LevelPlan p = plan_for(level);
            ASSERT_FALSE(p.heal);                                                                             // (off at every level: `--tune heal=1` turns it on)
            ASSERT_EQ(p.heal_hp, level == Level::Medium ? 3u : 4u);
            ASSERT_EQ(p.heal_danger_tiles, 3);
            ASSERT_EQ(p.heal_recent_ticks, 60u);
            ASSERT_EQ(p.heal_walk_tiles, level == Level::Medium ? 12 : 16);
            ASSERT_EQ(p.heal_calm_hp, level == Level::Medium ? 0u : 2u);
            LevelPlan off = p;
            without_heal(off);
            ASSERT_FALSE(off.heal);
            ASSERT_FALSE(LevelPlan{}.heal);                                                                   // (a plan made by hand has none of it)
        }
        struct Outcome {
            size_t commands;
            uint32_t started, orders;
            uint64_t hash;
            bool same_counters(const Outcome& o) const { return commands == o.commands && started == o.started && orders == o.orders; }
            bool operator==(const Outcome& o) const { return same_counters(o) && hash == o.hash; }
        };
        const auto play = [](const LevelPlan& plan) {
            sim::SimulationEngine sim;
            empty_field(sim, 367);
            sim.set_player_eggs(0, 5);
            for (int i = 0; i < 4; ++i) hurt(sim, sim::AntType::Worker, 10 + 2 * i, 3, 3 + static_cast<unsigned>(i % 2));
            hurt(sim, sim::AntType::Thief, 12, 8, 2);
            hurt(sim, sim::AntType::Bomber, 14, 8, 4);
            for (int i = 0; i < 4; ++i) foe(sim, 11 + 2 * i, 4);
            add_pile(sim, 30, 30, 40, 25);
            Rig rig(sim, 0, plan.level, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(500);
            const HealTask& h = rig.as<StandardBot>().heal();
            return Outcome{rig.proposed.size(), h.started(), h.orders(), sim.state_hash().total};
        };
        for (const Level level : {Level::Medium, Level::Easy, Level::Hard}) {
            const Outcome shipped = play(plan_for(level));
            ASSERT_EQ(shipped.started, 0u);
            ASSERT_EQ(shipped.orders, 0u);
            LevelPlan odd = plan_for(level);                                                                 // the numbers of the batch turned to extremes: with the flag off they change nothing
            odd.heal_hp = 10;
            odd.heal_danger_tiles = 90;
            odd.heal_recent_ticks = 9000;
            odd.heal_walk_tiles = 900;
            odd.heal_calm_hp = 10;
            ASSERT_TRUE(shipped == play(odd));
            LevelPlan off = plan_for(level);
            without_heal(off);
            ASSERT_TRUE(shipped == play(off));
        }
        LevelPlan medium_on = plan_for(Level::Medium);                                                       // (the scene sees the task: Medium with it built and turned on goes, so that the equality above means something)
        medium_on.heal = true;
        const Outcome on = play(medium_on);
        ASSERT_TRUE(on.started >= 1u && on.orders >= 1u);
        ASSERT_FALSE(on == play(plan_for(Level::Medium)));
        const Outcome hard_on = play(hard_plan());
        ASSERT_TRUE(hard_on.started >= 1u);
        ASSERT_FALSE(hard_on == play(plan_for(Level::Hard)));                                                // (the shipped Hard plan is not the plan with the task on)
        ASSERT_FALSE(hard_on == play(shipped(Level::Hard, false)));
    } TEST_END();

    TEST_CASE("AI36.7 The Numbers Of Who Is Sent (heal_wanted): The Hit Points At Most heal_hp, A Striker Near Or A Blow In The Window Or (No Danger) The Top-Up At Most heal_calm_hp With A Walk Of At Most 6 Tiles, And The Walk At Most heal_walk_tiles; The Walk Home In Tiles; The Enemy Ants That Are Near")
    {
        const LevelPlan hard = hard_plan();
        LevelPlan medium = plan_for(Level::Medium);
        medium.heal = true;
        // the hit points
        ASSERT_TRUE(heal_wanted(hard, 4, true, false, 5));
        ASSERT_FALSE(heal_wanted(hard, 5, true, false, 5));
        ASSERT_TRUE(heal_wanted(hard, 1, true, false, 5));
        ASSERT_TRUE(heal_wanted(medium, 3, true, false, 5));
        ASSERT_FALSE(heal_wanted(medium, 4, true, false, 5));
        // a reason: the danger, a blow, or the top-up; none of them: nobody goes
        ASSERT_TRUE(heal_wanted(hard, 4, false, true, 5));
        ASSERT_TRUE(heal_wanted(hard, 4, true, false, 5));
        ASSERT_FALSE(heal_wanted(hard, 4, false, false, 5));
        ASSERT_FALSE(heal_wanted(hard, 3, false, false, 5));
        ASSERT_TRUE(heal_wanted(hard, 2, false, false, 5));
        ASSERT_TRUE(heal_wanted(hard, 1, false, false, 5));
        // the top-up: at most heal_calm_hp (2) hit points and at most 6 tiles; Medium has none
        ASSERT_TRUE(heal_wanted(hard, 2, false, false, 6));
        ASSERT_FALSE(heal_wanted(hard, 2, false, false, 7));
        ASSERT_FALSE(heal_wanted(hard, 3, false, false, 6));
        ASSERT_FALSE(heal_wanted(medium, 1, false, false, 1));
        LevelPlan calm = hard;
        calm.heal_calm_hp = 3;
        ASSERT_TRUE(heal_wanted(calm, 3, false, false, 6));
        ASSERT_FALSE(heal_wanted(calm, 4, false, false, 6));
        // the walk limit (Hard 16, Medium 12), also with a reason; no walk at all (-1) never
        ASSERT_TRUE(heal_wanted(hard, 4, true, true, 16));
        ASSERT_FALSE(heal_wanted(hard, 4, true, true, 17));
        ASSERT_TRUE(heal_wanted(medium, 3, true, true, 12));
        ASSERT_FALSE(heal_wanted(medium, 3, true, true, 13));
        ASSERT_FALSE(heal_wanted(hard, 4, true, true, -1));
        ASSERT_FALSE(heal_wanted(hard, 1, false, false, -1));
        ASSERT_TRUE(heal_wanted(hard, 4, true, true, 0));
        // the walk home in tiles: the cost field of the hill (20 a step over grass, 28 a diagonal step), rounded up
        {
            World w;
            w.begin();
            const MapInfo& map = *w.h.map;
            ASSERT_EQ(walk_home_tiles(map, 0, TileCoord{6, 3}), 0);                                           // (the queue row costs 0)
            ASSERT_EQ(walk_home_tiles(map, 0, TileCoord{7, 3}), 1);
            ASSERT_EQ(walk_home_tiles(map, 0, TileCoord{12, 3}), 6);
            ASSERT_EQ(walk_home_tiles(map, 0, TileCoord{22, 3}), 16);
            ASSERT_EQ(walk_home_tiles(map, 0, TileCoord{8, 2}), 3);                                           // (one diagonal step and one straight: 48 is 2.4 steps, rounded up)
            ASSERT_EQ(walk_home_tiles(map, 0, TileCoord{13, 5}), 8);
            ASSERT_EQ(walk_home_tiles(map, 0, TileCoord{-1, 3}), -1);                                         // (off the map)
            ASSERT_EQ(walk_home_tiles(map, 0, TileCoord{60, 3}), -1);
            ASSERT_EQ(walk_home_tiles(map, 0, TileCoord{3, 60}), -1);
        }
        {   // a tile that no walk joins to the hill: -1 (enclosed by rocks)
            World w;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx != 0 || dy != 0) w.h.sim.set_terrain(30 + dx, 30 + dy, sim::TERRAIN_OBSTACLE);
                }
            }
            w.begin();
            ASSERT_EQ(walk_home_tiles(*w.h.map, 0, TileCoord{30, 30}), -1);
        }
        // the enemy ants that are near: within the radius (Chebyshev, the edge included), one that carries food is none, an ally's is none
        for (const int variant : {0, 1, 2}) {                                                                // 0: as they stand, 1: the near one carries food, 2: the near one is an ally's
            World w;
            hurt(w.h.sim, sim::AntType::Worker, 20, 20, 4);
            const uint32_t near_enemy = foe(w.h.sim, 23, 21);                                                // (3 tiles off in x, 1 in y)
            foe(w.h.sim, 20, 25, sim::AntType::Thief);                                                       // (5 tiles off)
            if (variant == 1) {
                w.h.sim.get_unit(near_enemy).holding = 1;
                w.h.sim.get_unit(near_enemy).carried_points = 20;
            }
            if (variant == 2) {
                w.h.sim.propose_alliance(1, 0);
                w.h.sim.accept_alliance(0, 1);
            }
            w.begin();
            const BotView view = BotView::build(w.h.sim, 0, w.h.map.get());
            ASSERT_EQ(view.ally() == 1, variant == 2);
            ASSERT_EQ(striker_near(view, TileCoord{20, 20}, 3), variant == 0);
            ASSERT_FALSE(striker_near(view, TileCoord{20, 20}, 2));
            ASSERT_EQ(striker_near(view, TileCoord{20, 20}, 5), variant != 2);                               // (the Thief 5 tiles off is a striker whatever its type, unless it is an ally's)
            ASSERT_FALSE(striker_near(view, TileCoord{20, 30}, 4));
        }
    } TEST_END();

    TEST_CASE("AI36.8 The Task By Hand, Who Is Taken: Hit Points At Most heal_hp, An Enemy Of Any Type Within The Radius Or A Blow Within The Window (Its Edges), The Top-Up, The Walk Limit, Every Type Of Ant, A Carrier; The Ants That Tasks Of Every Rank Hold: The Economy, The Pick-Ups, The Raids, The Walls, The Bombs, The Mines, The Strike, The Squad And The Sabotage Yes, A Fight, The Rush, The Kill, A Gate Goal And The Island Play No")
    {
        LevelPlan medium = plan_for(Level::Medium);
        medium.heal = true;
        const LevelPlan hard = hard_plan();
        // the hit points (Hard 4, Medium 3), the radius (3), the types
        ASSERT_TRUE(first_look(hard, sim::AntType::Worker, 4, 10, 3, 1).sent);
        ASSERT_FALSE(first_look(hard, sim::AntType::Worker, 5, 10, 3, 1).sent);
        ASSERT_TRUE(first_look(medium, sim::AntType::Worker, 3, 10, 3, 1).sent);
        ASSERT_FALSE(first_look(medium, sim::AntType::Worker, 4, 10, 3, 1).sent);
        ASSERT_TRUE(first_look(hard, sim::AntType::Worker, 4, 10, 3, 3).sent);
        ASSERT_FALSE(first_look(hard, sim::AntType::Worker, 4, 10, 3, 4).sent);
        ASSERT_FALSE(first_look(hard, sim::AntType::Worker, 4, 10, 3, -1).sent);                             // (no enemy at all)
        {
            LevelPlan wide = hard;
            wide.heal_danger_tiles = 6;
            ASSERT_TRUE(first_look(wide, sim::AntType::Worker, 4, 10, 3, 6).sent);                          // (the radius is the plan's)
            ASSERT_FALSE(first_look(wide, sim::AntType::Worker, 4, 10, 3, 7).sent);
            LevelPlan narrow = hard;
            narrow.heal_danger_tiles = 0;
            ASSERT_FALSE(first_look(narrow, sim::AntType::Worker, 4, 10, 3, 1).sent);
            ASSERT_TRUE(first_look(narrow, sim::AntType::Worker, 4, 10, 3, 0).sent);
            LevelPlan strong = hard;
            strong.heal_hp = 6;
            ASSERT_TRUE(first_look(strong, sim::AntType::Worker, 6, 10, 3, 1).sent);                        // (the hit points are the plan's)
            ASSERT_FALSE(first_look(strong, sim::AntType::Worker, 7, 10, 3, 1).sent);
            LevelPlan short_walk = hard;
            short_walk.heal_walk_tiles = 3;
            ASSERT_TRUE(first_look(short_walk, sim::AntType::Worker, 4, 9, 3, 1).sent);                     // (walk 3)
            ASSERT_FALSE(first_look(short_walk, sim::AntType::Worker, 4, 10, 3, 1).sent);                   // (walk 4)
        }
        for (const sim::AntType mine : {sim::AntType::Worker, sim::AntType::Combat, sim::AntType::Thief, sim::AntType::Fire, sim::AntType::Bomber}) {
            const Went w = first_look(hard, mine, 4, 10, 3, 2);
            ASSERT_TRUE(w.sent && w.held);
            for (const sim::AntType theirs : {sim::AntType::Worker, sim::AntType::Combat, sim::AntType::Thief, sim::AntType::Fire, sim::AntType::Bomber}) {
                ASSERT_TRUE(first_look(hard, mine, 4, 10, 3, 2, theirs).sent);                              // (an enemy of any type is a danger)
            }
        }
        // the top-up: 2 hit points or fewer and at most 6 tiles of walk, with no danger
        ASSERT_TRUE(first_look(hard, sim::AntType::Worker, 2, 12, 3, -1).sent);
        ASSERT_TRUE(first_look(hard, sim::AntType::Worker, 1, 12, 3, -1).sent);
        ASSERT_FALSE(first_look(hard, sim::AntType::Worker, 3, 12, 3, -1).sent);
        ASSERT_FALSE(first_look(hard, sim::AntType::Worker, 2, 13, 3, -1).sent);
        ASSERT_FALSE(first_look(medium, sim::AntType::Worker, 1, 8, 3, -1).sent);                           // (Medium has none)
        // the walk limit with a danger: Hard 16, Medium 12
        ASSERT_TRUE(first_look(hard, sim::AntType::Worker, 4, 22, 3, 1).sent);
        ASSERT_FALSE(first_look(hard, sim::AntType::Worker, 4, 23, 3, 1).sent);
        ASSERT_TRUE(first_look(medium, sim::AntType::Worker, 3, 18, 3, 1).sent);
        ASSERT_FALSE(first_look(medium, sim::AntType::Worker, 3, 19, 3, 1).sent);
        // an enemy that carries food, or an ally's, is no danger
        for (const int variant : {0, 1, 2}) {
            World w;
            const uint32_t ant = hurt(w.h.sim, sim::AntType::Worker, 10, 3, 4);
            const uint32_t enemy = foe(w.h.sim, 11, 3);
            if (variant == 1) w.h.sim.get_unit(enemy).pick_up_food(1, 25);
            if (variant == 2) {
                w.h.sim.propose_alliance(1, 0);
                w.h.sim.accept_alliance(0, 1);
            }
            w.begin();
            ASSERT_EQ(sent_home(w.h.look(), ant), variant == 0);
        }
        // a carrier goes (its click on the entrance banks the food and heals) and the task holds it
        {
            World w;
            const uint32_t carrier = carrier_at(w.h.sim, 0, TileCoord{10, 3});
            w.h.sim.get_unit(carrier).hp = 4;
            foe(w.h.sim, 11, 3);
            w.begin();
            ASSERT_TRUE(sent_home(w.h.look(), carrier));
            ASSERT_EQ(w.h.ledger().owner(carrier), StandardBot::kHeal);
        }
        // the window of a blow: the ant lost a hit point at tick T and nothing else is a reason; it is taken `after` ticks later, when a fight lets it go, if that is within heal_recent_ticks
        for (const uint64_t after : {uint64_t{0}, uint64_t{59}, uint64_t{60}, uint64_t{61}, uint64_t{200}}) {
            for (const uint32_t window : {60u, 10u}) {
                LevelPlan plan = hard;
                plan.heal_recent_ticks = window;
                World w(plan);
                const uint32_t ant = hurt(w.h.sim, sim::AntType::Worker, 10, 8, 5);                          // (walk 7: no top-up either)
                w.begin();
                ASSERT_TRUE(w.h.ledger().claim(ant, StandardBot::kFight));
                w.h.look();
                w.h.sim.get_unit(ant).hp = 4;
                w.h.tick(1);                                                                                 // (the view is made from the engine's snapshot, which a tick refreshes)
                w.h.look();                                                                                  // (the blow is seen here, at T)
                ASSERT_EQ(w.h.ledger().owner(ant), StandardBot::kFight);
                w.h.tick(after);
                ASSERT_TRUE(w.h.ledger().release(ant, StandardBot::kFight));
                ASSERT_EQ(sent_home(w.h.look(), ant), after <= window);
            }
        }
        // the tasks that hold an ant: the ranks below the fight's let it be taken, the others and the island play do not (the ledger keeps its owner)
        struct Owner {
            TaskId task;
            bool taken;
        };
        const Owner owners[] = {{StandardBot::kHarvest, true},   {StandardBot::kGuard, true},       {StandardBot::kFerry, true},     {StandardBot::kPowerUps, true}, {StandardBot::kRaids, true},
                                {StandardBot::kSabotage, true},  {StandardBot::kWalls, true},       {StandardBot::kBombs, true},     {StandardBot::kMines, true},    {StandardBot::kStrike, true},
                                {StandardBot::kHarass, true},    {StandardBot::kFight, false},      {StandardBot::kRush, false},     {StandardBot::kOpportunity, false}, {StandardBot::kGateGoals, false},
                                {StandardBot::kField, false},    {StandardBot::kIslands, false},    {StandardBot::kExpedition, false}};
        for (const Owner& k : owners) {
            World w;
            const uint32_t ant = hurt(w.h.sim, sim::AntType::Worker, 10, 3, 4);
            foe(w.h.sim, 11, 3);
            w.begin();
            ASSERT_TRUE(w.h.ledger().claim(ant, k.task));
            const Orders orders = w.h.look();
            ASSERT_EQ(sent_home(orders, ant), k.taken);
            ASSERT_EQ(w.h.ledger().owner(ant), k.taken ? StandardBot::kHeal : k.task);
            ASSERT_EQ(w.h.task->holds(ant), k.taken);
        }
        ASSERT_EQ(StandardBot::kHeal, kHealTaskId);
    } TEST_END();

    TEST_CASE("AI36.9 The Task By Hand, Who Is Not Taken: A Thief Of The Raid Task Within 6 Tiles Of The Raid Tile Of An Enemy Hill, The Keeper Of The Walls While An Enemy Thief Is Within 4 Tiles Of The Raid Tile Of The Own Hill, An Ant On A Power-Up, On The Mound, In Its Hit Clip, A Hill That Is Closed Or Not There, A Ring Of Fire Walls Round The Gate (The Walk Is Then Refused)")
    {
        // the Thief of the raid task: 6 tiles from the raid tile (53, 6) of the hill of seat 1 it is about to raid, 7 tiles it is not; another task's Thief goes either way
        for (const int32_t x : {47, 46}) {
            for (const TaskId owner : {StandardBot::kRaids, StandardBot::kPowerUps}) {
                LevelPlan plan = hard_plan();
                plan.heal_walk_tiles = 400;
                World w(plan);
                const uint32_t thief = hurt(w.h.sim, sim::AntType::Thief, x, 6, 4);
                foe(w.h.sim, x + 1, 6);
                w.begin();
                ASSERT_TRUE(w.h.ledger().claim(thief, owner));
                ASSERT_EQ(sent_home(w.h.look(), thief), !(owner == StandardBot::kRaids && x == 47));
            }
        }
        // the keeper of the walls: an enemy Thief 4 tiles from the raid tile (7, 6) of the own hill keeps it there, 5 tiles it does not; no demand, or another ant, or an enemy that is no Thief: it goes
        struct Keeper {
            int32_t thief_x;
            sim::AntType kind;
            bool demand;
            bool is_keeper;
            bool goes;
        };
        for (const Keeper k : {Keeper{11, sim::AntType::Thief, true, true, false}, Keeper{12, sim::AntType::Thief, true, true, true}, Keeper{11, sim::AntType::Thief, false, true, true},
                               Keeper{11, sim::AntType::Thief, true, false, true}, Keeper{11, sim::AntType::Worker, true, true, true}}) {
            World w;
            const uint32_t keeper = hurt(w.h.sim, sim::AntType::Fire, 10, 3, 4);
            foe(w.h.sim, k.thief_x, 6, k.kind);                                                              // (3 tiles from the Fire Ant, 4 or 5 from the raid tile)
            w.begin();
            w.h.tactics.wall_demand = k.demand;
            w.h.tactics.wall_keeper = k.is_keeper ? keeper : keeper + 99;
            ASSERT_EQ(sent_home(w.h.look(), keeper), k.goes);
        }
        // an ant that stands on a power-up (it cannot be struck; a Fire Ant does not take one, so it stays there) is not ordered; on the tile next to it it is. The power-up is one that landed after the start
        // (a flower's drop): the map's analysis does not know it, so no cost of the walk hides the rule
        for (const int32_t pad : {10, 11}) {
            World w;
            const uint32_t ant = hurt(w.h.sim, sim::AntType::Fire, 10, 3, 4);
            foe(w.h.sim, 12, 3);
            w.begin();
            w.h.sim.grid_mut().place_powerup(pad, 3, 2);
            w.h.tick(1);
            ASSERT_EQ(sent_home(w.h.look(), ant), pad != 10);
        }
        // the mound: an ant on the ramp is at the door (it is not ordered, nor held: the tiles of the mound have no walk, the cost field says -1); on the queue row it is ordered
        for (const int32_t y : {4, 3}) {
            World w;
            const uint32_t ant = hurt(w.h.sim, sim::AntType::Worker, 5, y, 4);
            foe(w.h.sim, 6, y);
            w.begin();
            const Orders orders = w.h.look();
            ASSERT_EQ(sent_home(orders, ant), y == 3);
            ASSERT_EQ(w.h.task->holds(ant), y == 3);
        }
        // in its hit clip (it cannot take an order) it is not ordered; when the clip is over it is, a few ticks later
        {
            World w;
            const uint32_t ant = hurt(w.h.sim, sim::AntType::Worker, 10, 3, 5);
            const uint32_t enemy = foe(w.h.sim, 11, 3, sim::AntType::Combat);                                // (a Combat Ant's punch throws the ant four tiles)
            w.begin();
            w.h.look();
            w.h.sim.apply_command(command_of(CommandType::GroupAttack, 1, {enemy}, 10, 3));
            bool clip = false;
            bool in_clip = false;
            bool after = false;
            for (int t = 0; t < 120; ++t) {
                w.h.tick(1);
                const sim::UnitState state = snapshot_of(w.h.sim, ant)->state;
                const bool hit = state == sim::UnitState::Flinch || state == sim::UnitState::Knockback;
                const bool sent = sent_home(w.h.look(), ant);
                clip = clip || hit;
                in_clip = in_clip || (hit && sent);
                after = after || (clip && !hit && sent);
                if (state == sim::UnitState::Stunned) ASSERT_FALSE(sent);                                    // (a stunned ant is no better off with an order; no flow of the engine that this world has shows the state)
            }
            ASSERT_TRUE(clip);
            ASSERT_FALSE(in_clip);
            ASSERT_TRUE(after);
        }
        // the hill of the game type 187 is closed; with the rule of the earlier matches it is open; a seat with no hill has nowhere to go
        for (const int variant : {0, 1, 2}) {
            World w(hard_plan(), variant == 1 ? sim::GameMode::HighestScore : sim::GameMode::Kills187, variant == 2);
            const uint32_t ant = hurt(w.h.sim, sim::AntType::Worker, 10, 3, 4);
            foe(w.h.sim, 11, 3);
            w.begin();
            ASSERT_EQ(w.h.sim.own_hill_enterable(), variant != 0);
            ASSERT_EQ(sent_home(w.h.look(), ant), variant != 0);
            ASSERT_EQ(w.h.task->holds(ant), variant != 0);
        }
        {
            Harness h;
            h.sim.init_test_world(60, 60, 36, 14400u * sim::TICK_MS);
            h.sim.grid_mut().set_anthill(1, kFightHills[1]);                                                 // (seat 0 has no hill)
            const uint32_t ant = hurt(h.sim, sim::AntType::Worker, 10, 3, 4);
            foe(h.sim, 11, 3);
            h.begin();
            ASSERT_FALSE(h.map->hill(0).present);
            ASSERT_FALSE(sent_home(h.look(), ant));
            ASSERT_EQ(h.task->jobs(), 0u);
        }
        // a ring of fire walls round the gate shuts the hill: no click into it is carried out, so none is made and the ant is not held; without the check (cantgo_aware off) it is
        for (const bool aware : {true, false}) {
            LevelPlan plan = hard_plan();
            plan.cantgo_aware = aware;
            World w(plan);
            const uint32_t ant = hurt(w.h.sim, sim::AntType::Worker, 14, 3, 4);
            foe(w.h.sim, 15, 3);
            w.begin();
            for (const TileCoord& t : SabotageTask::ring_of(w.h.map->hill(0))) w.h.sim.set_fire_at(t, 3600);
            const Orders orders = w.h.look();
            ASSERT_EQ(sent_home(orders, ant), !aware);
            ASSERT_EQ(w.h.task->holds(ant), !aware);
        }
    } TEST_END();

    TEST_CASE("AI36.10 The Order: One Group Move Onto The Entrance For Every Ant That Is Due At A Look, Urgent; An Ant That Walks Nearer Is Not Ordered Again, One That Stands Idle Or Walks Away Is After 40 Ticks, At Most 8 Times; After 600 Ticks The Ant Is Given Back And Left Alone For 600 More (An Ant In Its Enter Clip Is Waited For); The Ant That Is Healed Or Gone Is Given Back; One That A Gate Goal Takes Is Let Go")
    {
        // (a) one group order for the three ants that are due; the fourth, healthy, is not named
        {
            World w;
            std::vector<uint32_t> hurt_ants;
            for (int i = 0; i < 3; ++i) hurt_ants.push_back(hurt(w.h.sim, sim::AntType::Worker, 10 + i, 3, 3 + static_cast<unsigned>(i % 2)));
            const uint32_t healthy = hurt(w.h.sim, sim::AntType::Worker, 13, 3, 9);
            foe(w.h.sim, 12, 5);
            w.begin();
            const Orders orders = w.h.look();
            const std::vector<const Intent*> homes = homeward(orders);
            ASSERT_EQ(homes.size(), 1u);
            ASSERT_EQ(orders.intents().size(), 1u);
            ASSERT_TRUE(homes[0]->priority == Priority::Urgent);
            ASSERT_EQ(homes[0]->command.ants.size(), 3u);
            ASSERT_TRUE(std::is_sorted(homes[0]->command.ants.begin(), homes[0]->command.ants.end()));
            for (const uint32_t a : hurt_ants) ASSERT_TRUE(names(homes[0]->command, a));
            ASSERT_FALSE(names(homes[0]->command, healthy));
            ASSERT_EQ(w.h.task->orders(), 1u);
            ASSERT_EQ(w.h.task->ordered(), 3u);
            ASSERT_EQ(w.h.task->started(), 3u);
        }
        // (b) an ant that stands idle is ordered at once and again every 40 ticks, 8 orders in all; at 600 it is given back and, left alone for 600 ticks, taken again at 1,200
        {
            World w;
            const uint32_t ant = hurt(w.h.sim, sim::AntType::Worker, 10, 3, 4);
            foe(w.h.sim, 11, 3);
            w.begin();
            std::vector<uint64_t> at;                                                                        // the ticks (from the first look) at which it was ordered
            const uint64_t t0 = w.h.now();
            for (uint64_t t = 0; t <= 1230; t += 10) {
                if (t != 0) w.h.tick(10);
                if (sent_home(w.h.look(), ant)) at.push_back(w.h.now() - t0);
                if (t == 590) ASSERT_EQ(w.h.ledger().owner(ant), StandardBot::kHeal);
                if (t == 600) {
                    ASSERT_EQ(w.h.task->gave_up(), 1u);
                    ASSERT_EQ(w.h.ledger().owner(ant), kNoTask);
                }
                if (t == 1190) ASSERT_EQ(w.h.ledger().owner(ant), kNoTask);
            }
            ASSERT_TRUE((at == std::vector<uint64_t>{0, 40, 80, 120, 160, 200, 240, 280, 1200}));
            ASSERT_EQ(w.h.task->started(), 2u);
        }
        // (c) an ant that walks home is not ordered again however long it takes; one that walks away is, once the gap has passed
        for (const bool home : {true, false}) {
            World w;
            const uint32_t ant = hurt(w.h.sim, sim::AntType::Worker, 16, 3, 3);
            foe(w.h.sim, 17, 3);
            w.begin();
            ASSERT_TRUE(sent_home(w.h.look(), ant));
            w.h.sim.apply_command(command_of(CommandType::GroupMove, 0, {ant}, home ? 6 : 40, 3));
            std::vector<uint64_t> at;
            const uint64_t t0 = w.h.now();
            for (uint64_t t = 10; t <= 80; t += 10) {                                                        // (a tile a tick: it walks 8 tiles in 80 ticks, and is at the queue row after that)
                w.h.tick(10);
                if (sent_home(w.h.look(), ant)) at.push_back(w.h.now() - t0);
            }
            if (home) {
                ASSERT_TRUE(at.empty());
            } else {
                ASSERT_FALSE(at.empty());
                ASSERT_EQ(at.front(), kGap);                                                                 // (its walk grew at once; the gap holds the order back until 40)
            }
        }
        // (d) the ant that was hit while it walked home at 9 hit points (the walk fell at the last look) is not ordered again when it falls to 3; one whose last look is older than 40 ticks is
        for (const uint64_t wait : {uint64_t{36}, uint64_t{40}, uint64_t{41}, uint64_t{44}}) {
            for (const unsigned hp : {3u, 1u}) {
                World w;
                const uint32_t ant = hurt(w.h.sim, sim::AntType::Worker, 20, 3, 9);
                w.begin();
                w.h.sim.apply_command(command_of(CommandType::GroupMove, 0, {ant}, 6, 3));
                w.h.tick(12);
                w.h.look();                                                                                  // (walking, hurt, no reason to go: its walk is noted)
                w.h.tick(wait - 1);
                w.h.sim.get_unit(ant).hp = static_cast<uint8_t>(hp);
                w.h.tick(1);                                                                                 // (the view is made from the engine's snapshot, which a tick refreshes)
                const Orders orders = w.h.look();
                const bool fresh = wait <= 40;                                                               // (the last look counts)
                ASSERT_EQ(sent_home(orders, ant), !fresh);                                                   // (nearer: held, no order; the last look too old: ordered)
                ASSERT_EQ(w.h.task->holds(ant), !(fresh && hp == 1));                                        // (with 1 hit point and walking nearer it is the engine's: not even held)
            }
        }
        // (d2) an ant that walks away (or at the same distance: one tick of walking often leaves the walk in whole tiles as it was) is not on its way home: it is ordered
        {
            World w;
            const uint32_t ant = hurt(w.h.sim, sim::AntType::Worker, 14, 3, 9);
            w.begin();
            w.h.sim.apply_command(command_of(CommandType::GroupMove, 0, {ant}, 40, 3));
            w.h.tick(15);
            w.h.look();                                                                                      // (walking, hurt, no reason to go: its walk is noted)
            w.h.sim.get_unit(ant).hp = 3;
            w.h.tick(1);
            const BotView view = BotView::build(w.h.sim, 0, w.h.map.get());
            ASSERT_TRUE(view.mine()[0].state == sim::UnitState::Walking);
            ASSERT_TRUE(sent_home(w.h.look(), ant));
        }
        // (d3) an ant that a blow threw into a pocket that no walk joins to the hill (the map's walking costs say -1 there) is not ordered again: the order would be refused. It is given back after 600 ticks. (The
        // check of the walk as the map is now, cantgo_aware, would say the same: it is off here, so that the costs alone decide)
        {
            LevelPlan plan = hard_plan();
            plan.cantgo_aware = false;
            World w(plan);
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx != 0 || dy != 0) w.h.sim.set_terrain(30 + dx, 30 + dy, sim::TERRAIN_OBSTACLE);
                }
            }
            const uint32_t ant = hurt(w.h.sim, sim::AntType::Worker, 20, 3, 4);
            foe(w.h.sim, 21, 3);
            w.begin();
            ASSERT_TRUE(sent_home(w.h.look(), ant));
            w.h.sim.get_unit(ant).set_tile_pos(30, 30);
            w.h.tick(1);
            size_t again = 0;
            for (uint64_t t = 0; t < 700; t += 10) {
                w.h.tick(10);
                again += sent_home(w.h.look(), ant) ? 1u : 0u;
            }
            ASSERT_EQ(walk_home_tiles(*w.h.map, 0, TileCoord{30, 30}), -1);
            ASSERT_EQ(again, 0u);
            ASSERT_EQ(w.h.task->ordered(), 1u);
            ASSERT_EQ(w.h.task->gave_up(), 1u);
        }
        // (d4) an order that the engine refuses twice (the ant shows the can't-go reaction) ends the job: the map's costs and the walls of fire say that a walk exists, rocks that were not there at the start say
        // otherwise (the check of the walk is off here, so that the order is given at all)
        {
            LevelPlan plan = hard_plan();
            plan.cantgo_aware = false;
            World w(plan);
            const uint32_t ant = hurt(w.h.sim, sim::AntType::Worker, 14, 12, 4);
            foe(w.h.sim, 15, 12);
            w.begin();
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx != 0 || dy != 0) w.h.sim.set_terrain(14 + dx, 12 + dy, sim::TERRAIN_OBSTACLE);
                }
            }
            w.h.tick(1);
            size_t orders = 0;
            for (uint64_t t = 0; t < 400; t += 4) {
                const Orders o = w.h.look();
                for (const Intent& in : o.intents()) {
                    ++orders;
                    Command c = in.command;
                    c.issuer = 0;
                    w.h.sim.apply_command(c);                                                                // (what the controller would release)
                }
                w.h.tick(4);
            }
            ASSERT_EQ(w.h.task->gave_up(), 1u);
            ASSERT_TRUE(orders >= 2u && orders <= 3u);                                                       // (the first, one after the can't-go clip that the first made, and no more once the second is seen)
            ASSERT_EQ(w.h.ledger().owner(ant), kNoTask);
        }
        // (e) done: hit points back to 10 gives the ant back; gone does too; a gate goal (rank 6) that takes it is let go (the ledger keeps the goal's claim)
        {
            World w;
            const uint32_t ant = hurt(w.h.sim, sim::AntType::Worker, 10, 3, 4);
            const uint32_t gone = hurt(w.h.sim, sim::AntType::Worker, 11, 3, 4);
            const uint32_t taken = hurt(w.h.sim, sim::AntType::Worker, 12, 3, 4);
            foe(w.h.sim, 11, 4);
            w.begin();
            w.h.look();
            ASSERT_EQ(w.h.task->jobs(), 3u);
            ASSERT_TRUE(w.h.ledger().take(taken, StandardBot::kGateGoals));
            w.h.sim.get_unit(ant).hp = 10;
            w.h.sim.get_unit(gone).hp = 0;
            w.h.tick(2);
            w.h.look();
            ASSERT_EQ(w.h.task->jobs(), 0u);
            ASSERT_EQ(w.h.task->healed(), 1u);
            ASSERT_EQ(w.h.task->gave_up(), 0u);
            ASSERT_EQ(w.h.ledger().owner(ant), kNoTask);
            ASSERT_EQ(w.h.ledger().owner(gone), kNoTask);
            ASSERT_EQ(w.h.ledger().owner(taken), StandardBot::kGateGoals);
        }
        // (f) an ant in its enter clip when 600 ticks are up is waited for and healed, not given up on
        {
            World w;
            const uint32_t ant = hurt(w.h.sim, sim::AntType::Worker, 6, 3, 4);
            foe(w.h.sim, 7, 3);
            w.begin();
            const uint64_t t0 = w.h.now();
            bool entering_late = false;
            for (uint64_t t = 0; t <= 760; t += 4) {
                if (t != 0) w.h.tick(4);
                if (t == 556) w.h.sim.apply_command(command_of(CommandType::GroupMove, 0, {ant}, kEntrance.x, kEntrance.y));
                w.h.look();
                const sim::AntSnapshot* a = snapshot_of(w.h.sim, ant);
                entering_late = entering_late || (w.h.now() - t0 >= 600 && a != nullptr && a->state == sim::UnitState::EnteringBase);
            }
            ASSERT_TRUE(entering_late);                                                                      // (the premise: it was in its clip after 600 ticks)
            ASSERT_EQ(w.h.task->gave_up(), 0u);
            ASSERT_EQ(w.h.task->healed(), 1u);
        }
    } TEST_END();

    TEST_CASE("AI36.11 The Fights And The Other Tasks: A Fight Never Takes An Ant That The Task Holds (Without The Task The Same Ant Is A Defender), The Task Takes The Ant That A Fight Lets Go At The Same Look, And The Unjam Of The Ramp Leaves A Healing Ant Where It Is")
    {
        // (a) the hurt ant is held by the task before the blow on its neighbour: the fight (which takes any ant of 4 hit points) never takes it
        for (const bool heal : {true, false}) {
            sim::SimulationEngine sim;
            empty_field(sim, 371);
            sim.set_player_eggs(0, 5);
            const uint32_t hurt_ant = hurt(sim, sim::AntType::Worker, 13, 5, 4);
            const uint32_t victim = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{14, 5});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{15, 5});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{16, 5});
            const uint32_t enemy = foe(sim, 14, 7);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(shipped(Level::Hard, heal)), 4, 4);
            rig.run(16);
            ASSERT_EQ(rig.as<StandardBot>().ledger().owner(hurt_ant) == StandardBot::kHeal, heal);
            std::set<uint32_t> defenders;
            bool fight_held = false;
            for (int t = 0; t < 200; ++t) {
                keep_attacking(sim, 1, enemy, victim);
                rig.tick();
                for (const uint32_t d : rig.as<StandardBot>().fight().defenders_of(enemy)) defenders.insert(d);
                fight_held = fight_held || rig.as<StandardBot>().ledger().owner(hurt_ant) == StandardBot::kFight;
            }
            ASSERT_TRUE(rig.as<StandardBot>().fight().fights_started() >= 1u);
            ASSERT_TRUE(defenders.size() >= 1u);                                                             // (the fight did take ants)
            if (heal) {
                ASSERT_EQ(defenders.count(hurt_ant), 0u);
                ASSERT_FALSE(fight_held);
            } else {
                ASSERT_EQ(defenders.count(hurt_ant), 1u);                                                    // (without the task it is a defender: the scene does reach it)
            }
        }
        // (b) the ant that a fight lets go (it fell below 3 hit points) is the task's at the very look: its owner goes from the fight to the task with no look between
        {
            sim::SimulationEngine sim;
            empty_field(sim, 372);
            sim.set_player_eggs(0, 5);
            const uint32_t struck = hurt(sim, sim::AntType::Worker, 12, 3, 6);
            for (int i = 0; i < 3; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{14 + i, 4});
            const uint32_t enemy = foe(sim, 12, 1);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(hard_plan()), 4, 4);
            rig.run(12);
            std::vector<TaskId> owners;                                                                      // the task that holds the struck ant at every tick at which a look was made
            size_t seen_looks = rig.looks.size();
            for (int t = 0; t < 600 && alive(sim, struck); ++t) {
                keep_attacking(sim, 1, enemy, struck);
                rig.tick();
                if (rig.looks.size() != seen_looks) {
                    seen_looks = rig.looks.size();
                    owners.push_back(rig.as<StandardBot>().ledger().owner(struck));
                }
            }
            bool fought = false;
            bool handed = false;
            for (size_t i = 0; i + 1 < owners.size(); ++i) {
                fought = fought || owners[i] == StandardBot::kFight;
                if (owners[i] == StandardBot::kFight && owners[i + 1] != StandardBot::kFight) handed = owners[i + 1] == StandardBot::kHeal;
            }
            ASSERT_TRUE(fought);
            ASSERT_TRUE(handed);
        }
        // (c) the unjam of the ramp (Hard's gate): an ant without food that stands idle on the ramp while a carrier waits at the doorstep is sent to the side, unless the task holds it
        for (const TaskId holder : {StandardBot::kHeal, StandardBot::kHarvest}) {
            sim::SimulationEngine sim;
            empty_field(sim, 373);
            LevelPlan plan = hard_plan();
            plan.guards = false;
            plan.combat_harvests = false;
            plan.sabotage = plan.raids = plan.assault = plan.raider_hunt = false;
            plan.war_bombers = plan.war_fires = plan.mine_per_pile = plan.mine_gate = plan.mine_home = 0;
            const TileCoord ramp{5, 4};
            const uint32_t on_ramp = sim.spawn_unit(0, sim::AntType::Combat, ramp);
            carrier_at(sim, 0, TileCoord{5, 3});
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            ASSERT_TRUE(rig.as<StandardBot>().ledger_for_labs().claim(on_ramp, holder));
            rig.run(600);
            const sim::AntSnapshot* a = snapshot_of(sim, on_ramp);
            ASSERT_TRUE(a != nullptr);
            const bool moved = a->tile_x != ramp.x || a->tile_y != ramp.y;
            if (holder == StandardBot::kHeal) {
                ASSERT_EQ(rig.as<StandardBot>().gate().ramp_unjams(), 0u);
                ASSERT_FALSE(moved);
            } else {
                ASSERT_TRUE(rig.as<StandardBot>().gate().ramp_unjams() >= 1u);                               // (the scene reaches the unjam)
                ASSERT_TRUE(moved);
            }
        }
        // (d) the carrier aid (it plays at Hard only without the gate): a carrier that a blow hit is clicked home 8 ticks later when it stands idle, unless the task holds it; the task's own order is the one
        for (const bool heal : {true, false}) {
            LevelPlan plan = hard_plan();
            if (!heal) without_heal(plan);
            World w(plan);
            const uint32_t carrier = carrier_at(w.h.sim, 0, TileCoord{13, 3});
            w.h.sim.get_unit(carrier).hp = 5;
            w.begin();
            CarrierAidTask aid(StandardBot::kAid, w.h.tactics);
            w.h.tick(40);                                                                                    // (the aid orders an ant no sooner than 40 ticks into the match)
            size_t clicks = 0;
            clicks += homeward(w.h.look(&aid)).size();                                                       // (hp 5: no reason yet)
            w.h.sim.get_unit(carrier).hp = 3;
            w.h.tick(1);
            clicks += sent_home(w.h.look(&aid), carrier) ? 1u : 0u;                                          // (the blow is seen: the task takes the carrier and orders it; the aid notes it)
            w.h.tick(8);
            clicks += sent_home(w.h.look(&aid), carrier) ? 1u : 0u;                                          // (8 ticks on, idle: the aid's click, if the aid may)
            ASSERT_EQ(w.h.task->holds(carrier), heal);
            ASSERT_EQ(clicks, 1u);                                                                           // (one order in all: the task's, or without the task the aid's)
        }
    } TEST_END();
}
