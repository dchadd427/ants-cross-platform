// The kill batch (docs/BOT_FIGHT.md): a Hard bot strikes the kill that one blow gives, from the tile that makes it one, before any chore, and starts no fight that no walk reaches.
//
//   AI31.1  the model of a blow (knock_flight, kill_shot) against the engine: 240 random grounds of water, rocks, bombs and fire walls round a victim, a worker's blow or a Combat Ant's punch from a
//           random tile next to it: the engine kills the victim exactly when the model says so (water, a bomb, a fire wall, the blow itself) and otherwise lets it land where the model says
//   AI31.2  the scripted scenes: an enemy next to water and an own idle ant on the wrong side (a plain ant, a Combat Ant four tiles from the water): it walks to the right tile and the enemy drowns; the old hunts do not
//   AI31.3  priority: an ant that holds a chore (a rank 4 task) is taken, one that holds a pick-up (rank 3) is not; an ant that carries food never strikes; nothing happens with the plan off
//   AI31.4  reach: an enemy across a channel that no walk crosses gets no attack order (the assault of the free ants sent them before: "Can't go there."), one on the same shore does
//   AI31.5  the plans: Hard has the batch, Medium and Easy do not, the switch takes it out, a plan made by hand has none
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "ai_test.hpp"
#include "ants_ai/knock.hpp"
#include "ants_ai/standard_bot.hpp"
#include "b41_helpers.hpp"

using namespace ai_test;
using namespace ants;
using namespace ants::ai;
using namespace b41;

namespace {

// A small deterministic generator for the random grounds
struct Lcg {
    uint64_t s;
    uint32_t next() {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<uint32_t>(s >> 33);
    }
};

// A field with a lake row and no hills in the way: the victim's neighbourhood is made by the cases
void field(sim::SimulationEngine& sim, uint32_t seed) { empty_field(sim, seed, 3000); }

}  // namespace

void run_kill_tests() {
    TEST_CASE("AI31.1 The Model Of A Blow Against The Engine: A Worker's Blow Or A Combat Ant's Punch Kills Exactly When knock_flight / kill_shot Say So (Water, A Bomb, A Fire Wall, The Blow Itself), And Otherwise The Victim Lands Where They Say")
    {
        Lcg rng{12345};
        std::array<uint32_t, 5> kinds{};                                   // None, Blow, Water, Bomb, Fire
        uint32_t cornered = 0, landed_plain = 0, ran = 0;
        for (uint32_t k = 0; k < 240; ++k) {
            sim::SimulationEngine sim;
            field(sim, 300 + k);
            const TileCoord v{30, 30};
            for (int dy = -4; dy <= 4; ++dy) {
                for (int dx = -4; dx <= 4; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    const uint32_t r = rng.next() % 100;
                    const int x = v.x + dx, y = v.y + dy;
                    if (r < 20) sim.set_terrain(x, y, sim::TERRAIN_WATER);
                    else if (r < 26) sim.set_terrain(x, y, sim::TERRAIN_OBSTACLE);
                    else if (r < 38) sim.grid_mut().place_bomb(static_cast<uint32_t>(x), static_cast<uint32_t>(y), 2);
                    else if (r < 48) sim.grid_mut().place_firewall(static_cast<uint32_t>(x), static_cast<uint32_t>(y), 2);
                }
            }
            // a tile to strike from: a free neighbour (grass, nothing on it)
            std::vector<TileCoord> stands;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const TileCoord t{v.x + dx, v.y + dy};
                    if ((dx != 0 || dy != 0) && sim.grid().terrain_class_at(t) != sim::movement::kTerrainWater && !sim.grid().is_solid_object(t) && !sim.grid().has_bomb_at(t) && !sim.grid().has_fire_at(t)) stands.push_back(t);
                }
            }
            if (stands.empty()) continue;
            const TileCoord stand = stands[rng.next() % stands.size()];
            const sim::AntType striker_type = (rng.next() & 1u) != 0 ? sim::AntType::Combat : sim::AntType::Worker;
            if (k % 12 == 0 && striker_type == sim::AntType::Worker) {      // (some cornered victims: every tile that the flight may try, the direction away from the attacker and the two on each side, is a rock)
                const int away = sim::movement::dir_from_delta(v.y - stand.y, v.x - stand.x);
                for (const int turn : {0, 1, -1, 2, -2}) {
                    const sim::movement::TileDelta delta = sim::movement::dir_delta(static_cast<uint8_t>((away + turn + 8) % 8));
                    const int range = knock_range(striker_type);
                    sim.set_terrain(v.x + delta.dcol * range, v.y + delta.drow * range, sim::TERRAIN_OBSTACLE);
                }
            }
            const uint8_t hp = static_cast<uint8_t>(1 + rng.next() % ((k & 1u) != 0 ? 5u : 10u));         // (half of the victims are already hurt: a bomb or a fire wall only kills what is left with a few)
            const uint32_t attacker = sim.spawn_unit(0, striker_type, stand);
            const uint32_t victim = sim.spawn_unit(1, sim::AntType::Worker, v);
            sim.get_unit(victim).hp = hp;
            const KillShot shot = kill_shot(sim.grid(), v, hp, sim::AntType::Worker, stand, striker_type);
            const KnockFlight flight = knock_flight(sim.grid(), v, stand, knock_range(striker_type));
            ++kinds[static_cast<size_t>(shot.kind)];
            const bool plain = flight.flies && sim.grid().terrain_class_at(flight.land) != sim::movement::kTerrainWater && !sim.grid().has_bomb_at(flight.land) && !sim.grid().has_fire_at(flight.land);
            if (!flight.flies) ++cornered;
            sim.apply_command(command_of(CommandType::GroupAttack, 0, {attacker}, v.x, v.y));
            tick_all(sim, 200);
            ++ran;
            const bool dead = !alive(sim, victim);
            if (shot.kills()) {
                ASSERT_TRUE(dead);
            } else {
                const bool reflex = !flight.flies && striker_type == sim::AntType::Combat;         // (a cornered victim stays next to a Combat Ant, whose reflex punches it again and again)
                if ((plain || !flight.flies) && !reflex && hp > blow_hit_points(striker_type) + 1u) ASSERT_FALSE(dead);                // (a victim with 1 hit point left walks home through whatever lies on the way; a bomb or a fire wall that does not finish it throws it on, where the model does not say)
                const sim::AntSnapshot* s = snapshot_of(sim, victim);
                if (plain && hp >= 5 && s != nullptr) {                         // (a victim with 1 hit point left walks home: the ones with more stay where they landed)
                    ASSERT_EQ(s->tile_x, flight.land.x);
                    ASSERT_EQ(s->tile_y, flight.land.y);
                    ++landed_plain;
                }
                if (!flight.flies && !reflex && s != nullptr && hp >= 5) {
                    ASSERT_EQ(s->tile_x, v.x);
                    ASSERT_EQ(s->tile_y, v.y);
                }
            }
        }
        ASSERT_TRUE(ran >= 200u);
        ASSERT_TRUE(kinds[static_cast<size_t>(KillKind::Water)] >= 20u);        // (the cases cover every kind, or the test proves nothing)
        ASSERT_TRUE(kinds[static_cast<size_t>(KillKind::Blow)] >= 10u);
        ASSERT_TRUE(kinds[static_cast<size_t>(KillKind::Bomb)] >= 3u);
        ASSERT_TRUE(kinds[static_cast<size_t>(KillKind::Fire)] >= 3u);
        ASSERT_TRUE(kinds[static_cast<size_t>(KillKind::None)] >= 40u);
        ASSERT_TRUE(landed_plain >= 30u);
        ASSERT_TRUE(cornered >= 2u);
    } TEST_END();

    TEST_CASE("AI31.2 An Enemy Next To Water And An Own Idle Ant On The Wrong Side: The Ant Walks To The Tile Away From The Water And Strikes From There, The Enemy Drowns (A Plain Ant One Tile From The Water, A Combat Ant Four Tiles); The Hunts Of Before Do Not")
    {
        for (const bool opportunity : {true, false}) {
            for (const bool combat : {false, true}) {
                sim::SimulationEngine sim;
                field(sim, combat ? 402 : 401);
                const int row = combat ? 19 : 16;                            // the water row: the enemy stands one tile from it (a plain blow) or four (a punch)
                for (int x = 14; x <= 26; ++x) sim.set_terrain(x, row, sim::TERRAIN_WATER);
                const TileCoord enemy_at{20, combat ? 15 : 15};
                sim.spawn_unit(0, combat ? sim::AntType::Combat : sim::AntType::Worker, TileCoord{14, 15});   // on the west side of the enemy, in line with it: an attack from there throws it east, not into the water
                sim.spawn_unit(0, sim::AntType::Worker, TileCoord{8, 8});
                sim.spawn_unit(0, sim::AntType::Worker, TileCoord{9, 8});
                const uint32_t enemy = sim.spawn_unit(1, sim::AntType::Worker, enemy_at);
                LevelPlan plan = plan_for(Level::Hard);
                plan.assault = false;
                plan.opportunity = opportunity;
                if (opportunity) plan.water_hunt = false;                      // (so that the kill is the task's: the old hunts are on in the other half and do not get it)
                Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
                rig.run(260);
                StandardBot& bot = rig.as<StandardBot>();
                if (opportunity) {
                    ASSERT_FALSE(alive(sim, enemy));
                    ASSERT_TRUE(bot.opportunity().started() >= 1u);
                    if (!combat) {                                              // (a Combat Ant's reflex may punch the enemy on its way: the enemy is dead all the same)
                        ASSERT_TRUE(bot.opportunity().blows() >= 1u);
                        ASSERT_TRUE(bot.opportunity().kills() >= 1u);
                    }
                    ASSERT_EQ(bot.opportunity().jobs(), 0u);                    // (the ant was given back)
                } else {
                    ASSERT_TRUE(alive(sim, enemy));                              // the old hunts need the ant to stand in line with the water: it does not
                    ASSERT_EQ(bot.opportunity().started(), 0u);
                }
            }
        }
    } TEST_END();

    TEST_CASE("AI31.3 Priority: An Ant That Holds A Chore (Rank 4) Is Taken For The Kill, One That Holds A Pick-Up (Rank 3) Is Not, An Ant With Food Never Strikes, A Fire Ant Keeps Its Walls; An Enemy That Walks Is Struck Only By An Ant That Stands On The Right Tile")
    {
        struct Case {
            TaskId holder;                                                  // the task that holds the only ant near (kNoTask: none), with the rank below
            uint8_t rank;
            bool carries;
            sim::AntType type;
            bool expect_job;
        };
        const Case cases[] = {
            {kNoTask, 0, false, sim::AntType::Worker, true},
            {StandardBot::kBombs, 4, false, sim::AntType::Worker, true},    // (rank 4: a chore)
            {StandardBot::kHarvest, 1, false, sim::AntType::Worker, true},
            {StandardBot::kGuard, 2, false, sim::AntType::Worker, true},
            {StandardBot::kPowerUps, 3, false, sim::AntType::Worker, false},  // (rank 3: a pick-up is on its way)
            {StandardBot::kFight, 5, false, sim::AntType::Worker, false},   // (a fight of the same rank keeps its ants)
            {kNoTask, 0, true, sim::AntType::Worker, false},                // (it carries food)
            {kNoTask, 0, false, sim::AntType::Fire, false},                 // (the keeper of the walls)
            {kNoTask, 0, false, sim::AntType::Bomber, false},               // (the mines and the counters)
            {StandardBot::kBombs, 4, false, sim::AntType::Combat, false},   // (a Combat Ant of a squad: the squads are rank 4 as well)
        };
        for (const Case& cs : cases) {
            sim::SimulationEngine sim;
            field(sim, 411);
            for (int x = 14; x <= 26; ++x) sim.set_terrain(x, 16, sim::TERRAIN_WATER);
            const uint32_t striker = sim.spawn_unit(0, cs.type, TileCoord{14, 15});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{8, 8});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{9, 8});
            sim.spawn_unit(1, sim::AntType::Worker, TileCoord{20, 15});
            if (cs.carries) sim.get_unit(striker).holding = 1;
            const MapInfo map(sim);
            const BotView view = BotView::build(sim, 0, &map);
            Tactics tactics;
            tactics.plan = plan_for(Level::Hard);
            AntLedger ledger;
            ledger.set_rank(StandardBot::kOpportunity, 5);
            if (cs.holder != kNoTask) {
                ledger.set_rank(cs.holder, cs.rank);
                ASSERT_TRUE(ledger.claim(striker, cs.holder));
            }
            Orders orders;
            const Profile profile = profile_for(Level::Hard);
            TaskContext context{view, orders, ledger, profile, map, 0};
            OpportunityTask task(StandardBot::kOpportunity, tactics);
            task.step(context);
            ASSERT_EQ(task.started(), cs.expect_job ? 1u : 0u);
            if (cs.expect_job) ASSERT_EQ(ledger.owner(striker), StandardBot::kOpportunity);                  // (taken from the chore for the kill)
            else ASSERT_EQ(ledger.owner(striker), cs.holder);
        }
        {   // an enemy that walks along the shore is struck only by an ant that already stands on the right tile
            sim::SimulationEngine sim;
            field(sim, 412);
            for (int x = 10; x <= 30; ++x) sim.set_terrain(x, 16, sim::TERRAIN_WATER);
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{22, 12});      // far from the walk: nothing to strike from
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{8, 8});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{9, 8});
            const uint32_t walker = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{12, 15});
            sim.apply_command(command_of(CommandType::GroupMove, 1, {walker}, 28, 15));
            tick_all(sim, 12);                                               // (until it is drawn walking: an ordered ant whose path has not come yet still stands in its idle clip)
            LevelPlan plan = plan_for(Level::Hard);
            plan.assault = false;
            plan.water_hunt = false;
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(30);                                                     // (it walks along the shore: no ant is sent to a tile it will have left)
            ASSERT_EQ(rig.as<StandardBot>().opportunity().started(), 0u);
        }
        {   // an ant that already stands on the tile away from the water strikes an enemy that stands still next to it at once (no move order is needed)
            sim::SimulationEngine sim;
            field(sim, 413);
            for (int x = 14; x <= 26; ++x) sim.set_terrain(x, 16, sim::TERRAIN_WATER);
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{20, 14});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{8, 8});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{9, 8});
            const uint32_t enemy = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{20, 15});
            LevelPlan plan = plan_for(Level::Hard);
            plan.assault = false;
            plan.water_hunt = false;
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(60);
            ASSERT_FALSE(alive(sim, enemy));
            ASSERT_EQ(rig.as<StandardBot>().opportunity().ambushes(), 1u);
            ASSERT_EQ(rig.proposed_count(CommandType::GroupMove), 0u);
        }
    } TEST_END();

    TEST_CASE("AI31.4 Reach: The Free Ants Send No Attack Order At An Enemy That No Walk Reaches (Across A Channel), Where They Sent Many Before; An Enemy On Their Own Shore Is Still Attacked")
    {
        for (const bool reach : {true, false}) {
            for (const bool across : {true, false}) {
                sim::SimulationEngine sim;
                field(sim, 421);
                for (int y = 0; y < 60; ++y) {
                    for (int x = 25; x <= 27; ++x) sim.set_terrain(x, y, sim::TERRAIN_WATER);      // a channel from the top to the bottom of the field
                }
                for (int i = 0; i < 6; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{14 + i, 30});
                sim.spawn_unit(1, sim::AntType::Worker, across ? TileCoord{36, 30} : TileCoord{20, 36});
                LevelPlan plan = plan_for(Level::Hard);
                plan.reach_fights = reach;
                plan.opportunity = false;
                plan.assault_after = 100;
                plan.assault_odds_percent = 10;
                Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
                rig.run(700);
                const size_t attacks = rig.proposed_count(CommandType::GroupAttack);
                if (across && reach) ASSERT_EQ(attacks, 0u);
                else ASSERT_TRUE(attacks >= 1u);                            // (the bug that is fixed: an order the engine refuses, as often as the fights run; and the enemy that can be reached)
            }
        }
    } TEST_END();

    TEST_CASE("AI31.5 The Plans: Hard Has The Kill Batch, Medium And Easy Do Not, The Switch Takes It Out, A Plan Made By Hand Has None")
    {
        const LevelPlan hard = plan_for(Level::Hard);
        ASSERT_TRUE(hard.opportunity && hard.reach_fights);
        ASSERT_TRUE(hard.opportunity_reach >= 4 && hard.opportunity_jobs >= 1u && hard.opportunity_ticks >= 60u);
        for (const Level level : {Level::Easy, Level::Medium}) {
            const LevelPlan p = plan_for(level);
            ASSERT_FALSE(p.opportunity);
        }
        LevelPlan off = hard;
        without_kill_batch(off);
        ASSERT_FALSE(off.opportunity || off.reach_fights);
        const LevelPlan by_hand;
        ASSERT_FALSE(by_hand.opportunity || by_hand.reach_fights);
    } TEST_END();
}
