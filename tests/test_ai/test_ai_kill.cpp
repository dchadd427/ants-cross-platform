// The kill batch (docs/BOT_FIGHT.md): a Hard bot strikes the kill that one blow gives, from the tile that makes it one, before any chore, and starts no fight that no walk reaches.
//
//   AI31.1  the model of a blow (knock_flight, kill_shot) against the engine: 240 random grounds of water, rocks, bombs and fire walls round a victim, a worker's blow or a Combat Ant's punch from a
//           random tile next to it: the engine kills the victim exactly when the model says so (water, a bomb, a fire wall, the blow itself) and otherwise lets it land where the model says
//   AI31.2  the scripted scenes: an enemy next to water and an own idle ant on the wrong side (a plain ant, a Combat Ant four tiles from the water): it walks to the right tile and the enemy drowns; the old hunts do not
//   AI31.2c a walk that takes longer than half the clock of a job is not begun
//   AI31.8  what is worth a trip: the enemy has stood a second, is no Combat Ant at rest, the economy's ants go only a short way
//   AI31.3  priority: an ant that holds a chore (a rank 4 task) is taken, one that holds a pick-up (rank 3) is not; an ant that carries food never strikes; nothing happens with the plan off
//   AI31.4  reach: an enemy across a channel that no walk crosses gets no attack order (the assault of the free ants sent them before: "Can't go there."), one on the same shore does
//   AI31.5  the plans: Hard has the batch, Medium and Easy do not, the switch takes it out, a plan made by hand has none
//   AI31.7  chores: a ring of fire walls round an enemy gate is lit only while that team has food to bank (a pile its hill reaches, a carrier); the three walls of the own thief hole are not touched by the rule
//   AI31.6  idle hunt: three ants with no food to reach and no egg (the reserve of a fight kept them home for ever on the arena maps) go for an enemy; with food to reach, or with the rule off, they stay
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "ai_test.hpp"
#include "ants_ai/chore_worth.hpp"
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
        {   // a Swimmer that lands in water only splashes: no kill (and a worker in the same place drowns)
            sim::SimulationEngine sim;
            field(sim, 399);
            for (int x = 28; x <= 32; ++x) sim.set_terrain(x, 31, sim::TERRAIN_WATER);
            ASSERT_TRUE(kill_shot(sim.grid(), TileCoord{30, 30}, 10, sim::AntType::Worker, TileCoord{30, 29}, sim::AntType::Worker).kind == KillKind::Water);
            ASSERT_FALSE(kill_shot(sim.grid(), TileCoord{30, 30}, 10, sim::AntType::Swimmer, TileCoord{30, 29}, sim::AntType::Worker).kills());
        }
        {   // the edges of the model: the blow itself kills exactly what it takes (a worker 1 hit point, a Combat Ant 2), a fire wall under the landing what is left with 1, a bomb what is left with 2
            sim::SimulationEngine sim;
            field(sim, 400);
            const TileCoord victim{30, 40};
            const TileCoord stand{30, 39};
            const auto shot = [&](uint8_t hp, sim::AntType striker) { return kill_shot(sim.grid(), victim, hp, sim::AntType::Worker, stand, striker); };
            ASSERT_TRUE(shot(1, sim::AntType::Worker).kind == KillKind::Blow);
            ASSERT_FALSE(shot(2, sim::AntType::Worker).kills());
            ASSERT_TRUE(shot(2, sim::AntType::Combat).kind == KillKind::Blow);
            ASSERT_FALSE(shot(3, sim::AntType::Combat).kills());
            sim.grid_mut().place_firewall(30, 41, 2);                            // (where a worker's blow lands the victim)
            ASSERT_TRUE(shot(2, sim::AntType::Worker).kind == KillKind::Fire);
            ASSERT_FALSE(shot(3, sim::AntType::Worker).kills());
            ASSERT_FALSE(kill_shot(sim.grid(), victim, 2, sim::AntType::Fire, stand, sim::AntType::Worker).kills());     // (a Fire Ant walks through fire)
        }
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

    TEST_CASE("AI31.2b The Walk Is Asked Of The Map As It Is Now: A Striker That Fire Walls (Lit After The Start) Have Shut In Is Not Sent To The Tile It Would Strike From (No Order That The Engine Refuses)")
    {
        sim::SimulationEngine sim;
        field(sim, 405);
        for (int x = 14; x <= 26; ++x) sim.set_terrain(x, 16, sim::TERRAIN_WATER);
        const uint32_t striker = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{12, 12});
        sim.spawn_unit(0, sim::AntType::Worker, TileCoord{8, 8});
        sim.spawn_unit(0, sim::AntType::Worker, TileCoord{9, 8});
        const uint32_t enemy = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{20, 15});
        LevelPlan plan = plan_for(Level::Hard);
        plan.assault = false;
        plan.water_hunt = false;
        Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);        // (the analysis of the start is made here: the way is open)
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx != 0 || dy != 0) sim.grid_mut().place_firewall(static_cast<uint32_t>(12 + dx), static_cast<uint32_t>(12 + dy), 3);     // a ring of fire round the striker
            }
        }
        rig.run(200);
        ASSERT_EQ(rig.as<StandardBot>().opportunity().started(), 0u);
        ASSERT_TRUE(alive(sim, enemy));
        (void)striker;
    } TEST_END();

    TEST_CASE("AI31.2c A Walk That Is Too Long Is Not Begun: The Striker Stands Within Reach Of The Tile But On The Other Side Of A Long Channel (A Detour Of Many Seconds), So No Job Starts; With A Long Enough Clock It Does")
    {
        for (const uint32_t ticks : {240u, 6000u}) {
            sim::SimulationEngine sim;
            field(sim, 406);
            for (int x = 3; x <= 56; ++x) sim.set_terrain(x, 16, sim::TERRAIN_WATER);          // a channel with a gap far to the west
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{20, 18});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{30, 40});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{31, 40});
            const uint32_t enemy = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{20, 15});          // (drowns when struck from the north)
            LevelPlan plan = plan_for(Level::Hard);
            plan.assault = false;
            plan.water_hunt = false;
            plan.opportunity_ticks = ticks;
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(120);
            if (ticks < 1000u) ASSERT_EQ(rig.as<StandardBot>().opportunity().started(), 0u);
            else ASSERT_TRUE(rig.as<StandardBot>().opportunity().started() >= 1u);
            ASSERT_TRUE(alive(sim, enemy) || ticks >= 1000u);
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
            tactics.plan.opportunity_still = 0;                             // (the ranks are tested here: the other rules of a job have their own test, AI31.8)
            tactics.plan.opportunity_harvest_d = 99;
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

    TEST_CASE("AI31.8 What Is Worth A Trip: An Enemy That Has Not Stood For A Second Is Not Gone For (It Is Gone When The Striker Comes), A Combat Ant At Rest Is Not Gone For (Its Reflex Meets The Striker), An Ant Of The Economy Goes Only For A Kill That Is Close; An Ant That Stands On The Right Tile Strikes At Once")
    {
        struct Case {
            sim::AntType enemy_type;
            uint32_t still;                                                 // plan.opportunity_still
            uint32_t wait;                                                  // ticks the enemy stands before the look
            bool skip_guard;
            TaskId holder;
            int striker_x;                                                  // the stand is (20, 14): this many tiles away
            int32_t harvest_d;
            bool expect_job;
        };
        const Case cases[] = {
            {sim::AntType::Worker, 20, 0, false, kNoTask, 16, 8, false},    // (it has just been seen: the first look)
            {sim::AntType::Worker, 20, 24, false, kNoTask, 16, 8, true},    // (it has stood for 24 ticks)
            {sim::AntType::Worker, 20, 24, false, kNoTask, 20, 8, true},    // (an ant on the tile strikes whatever the clock)
            {sim::AntType::Worker, 20, 0, false, kNoTask, 20, 8, true},
            {sim::AntType::Worker, 0, 0, false, kNoTask, 16, 8, true},      // (the rule off)
            {sim::AntType::Combat, 0, 0, true, kNoTask, 16, 8, false},      // (a Combat Ant at rest: not by an ant that has to walk ...)
            {sim::AntType::Combat, 0, 0, true, kNoTask, 20, 8, true},       // (... but by one that stands on the tile)
            {sim::AntType::Combat, 0, 0, false, kNoTask, 16, 8, true},      // (the rule off)
            {sim::AntType::Worker, 0, 0, false, StandardBot::kHarvest, 16, 2, false},    // (an ant of the economy, 4 tiles: no)
            {sim::AntType::Worker, 0, 0, false, StandardBot::kHarvest, 18, 2, true},     // (2 tiles: yes)
            {sim::AntType::Worker, 0, 0, false, StandardBot::kHarvest, 16, 8, true},     // (the rule off)
            {sim::AntType::Worker, 0, 0, false, StandardBot::kBombs, 16, 2, true},       // (a chore of rank 4 is not the economy)
        };
        for (const Case& cs : cases) {
            sim::SimulationEngine sim;
            field(sim, 414);
            for (int x = 14; x <= 26; ++x) sim.set_terrain(x, 16, sim::TERRAIN_WATER);
            const uint32_t striker = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{cs.striker_x, 14});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{8, 8});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{9, 8});
            sim.spawn_unit(1, cs.enemy_type, TileCoord{20, 15});
            tick_all(sim, 2);
            Tactics tactics;
            tactics.plan = plan_for(Level::Hard);
            tactics.plan.opportunity_still = cs.still;
            tactics.plan.opportunity_skip_guard = cs.skip_guard;
            tactics.plan.opportunity_harvest_d = cs.harvest_d;
            AntLedger ledger;
            ledger.set_rank(StandardBot::kOpportunity, 5);
            if (cs.holder != kNoTask) {
                ledger.set_rank(cs.holder, cs.holder == StandardBot::kHarvest ? 1 : 4);
                ASSERT_TRUE(ledger.claim(striker, cs.holder));
            }
            const Profile profile = profile_for(Level::Hard);
            OpportunityTask task(StandardBot::kOpportunity, tactics);
            {   // the first look: the clock of the enemy starts here
                const MapInfo map(sim);
                const BotView view = BotView::build(sim, 0, &map);
                Orders orders;
                TaskContext context{view, orders, ledger, profile, map, 0};
                task.step(context);
            }
            if (cs.wait > 0) tick_all(sim, cs.wait);
            if (cs.wait > 0 || cs.still == 0 || cs.striker_x == 20) {
                const MapInfo map(sim);
                const BotView view = BotView::build(sim, 0, &map);
                Orders orders;
                TaskContext context{view, orders, ledger, profile, map, 0};
                task.step(context);
            }
            ASSERT_EQ(task.started(), cs.expect_job ? 1u : 0u);
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
                if (across && reach) {
                    ASSERT_EQ(attacks, 0u);
                    ASSERT_EQ(rig.as<StandardBot>().fight().fights_started(), 0u);        // (no fight is formed of ants that no walk takes to the enemy: they would be held for nothing)
                }
                else ASSERT_TRUE(attacks >= 1u);                            // (the bug that is fixed: an order the engine refuses, as often as the fights run; and the enemy that can be reached)
            }
        }
    } TEST_END();

    TEST_CASE("AI31.4b Reach: The Ants That Answer A Blow Are Taken From The Shore Of The Fight, Not From Across A Channel (They Would Be Ordered Into \"Can't Go There.\")")
    {
        for (const bool reach : {true, false}) {
            sim::SimulationEngine sim;
            field(sim, 422);
            sim.set_player_eggs(0, 5);
            for (int y = 0; y < 60; ++y) {
                for (int x = 25; x <= 27; ++x) sim.set_terrain(x, y, sim::TERRAIN_WATER);
            }
            const uint32_t hit = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{22, 30});
            const uint32_t near_a = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{20, 32});
            std::vector<uint32_t> across;
            for (int i = 0; i < 3; ++i) across.push_back(sim.spawn_unit(0, sim::AntType::Worker, TileCoord{30 + i, 30}));        // on the other shore, nearer to the enemy than the ants of this one
            const uint32_t enemy = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{21, 30});
            LevelPlan plan = plan_for(Level::Hard);
            plan.opportunity = false;
            plan.assault = false;
            plan.reach_fights = reach;
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(3);
            for (int t = 0; t < 200; ++t) {
                keep_attacking(sim, 1, enemy, hit);
                rig.tick();
            }
            size_t from_across = 0;
            for (const auto& e : rig.proposed) {
                if (e.second.type != CommandType::GroupAttack) continue;
                for (const uint32_t a : e.second.ants) from_across += (a == across[0] || a == across[1] || a == across[2]) ? 1u : 0u;
            }
            ASSERT_TRUE(rig.proposed_count(CommandType::GroupAttack) >= 1u);                 // (the ants of this shore answer)
            if (reach) ASSERT_EQ(from_across, 0u);
            else ASSERT_TRUE(from_across >= 1u);
            (void)near_a;
        }
    } TEST_END();

    TEST_CASE("AI31.5 The Plans: Hard Has The Kill Batch, Medium And Easy Do Not, The Switch Takes It Out, A Plan Made By Hand Has None")
    {
        const LevelPlan hard = plan_for(Level::Hard);
        ASSERT_TRUE(hard.opportunity && hard.reach_fights && hard.idle_hunt);
        ASSERT_TRUE(hard.opportunity_reach >= 4 && hard.opportunity_jobs >= 1u && hard.opportunity_ticks >= 60u);
        for (const Level level : {Level::Easy, Level::Medium}) {
            const LevelPlan p = plan_for(level);
            ASSERT_FALSE(p.opportunity);
        }
        LevelPlan off = hard;
        without_kill_batch(off);
        ASSERT_FALSE(off.opportunity || off.reach_fights || off.idle_hunt);
        const LevelPlan by_hand;
        ASSERT_FALSE(by_hand.opportunity || by_hand.reach_fights || by_hand.idle_hunt);
    } TEST_END();

    TEST_CASE("AI31.6 Idle Hunt: Three Ants With No Food To Reach And No Egg Go For An Enemy That A Walk Reaches (The Reserve Of A Fight Kept Them Home For Ever); With Food To Reach Or With The Rule Off They Stay")
    {
        for (const bool idle_hunt : {true, false}) {
            for (const bool food : {false, true}) {
                sim::SimulationEngine sim;
                field(sim, 431);
                sim.set_player_eggs(0, 0);                                    // (no egg to fall back on: the reserve of a fight is three ants)
                sim.spawn_unit(0, sim::AntType::Worker, TileCoord{12, 12});
                sim.spawn_unit(0, sim::AntType::Worker, TileCoord{13, 12});
                sim.spawn_unit(0, sim::AntType::Worker, TileCoord{14, 12});
                sim.spawn_unit(1, sim::AntType::Worker, TileCoord{30, 30});
                if (food) place_pile(sim, 20, 12, 400, 25, {{400, 369}, {300, 370}, {200, 371}, {100, 372}, {0, kPileGone}});      // (a pile that the hill reaches and the ants do not empty in this time: the economy has work, so the three ants are the economy's)
                LevelPlan plan = plan_for(Level::Hard);
                plan.opportunity = false;
                plan.idle_hunt = idle_hunt;
                plan.assault_after = 200;
                Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
                rig.run(1400);
                const size_t attacks = rig.proposed_count(CommandType::GroupAttack);
                if (idle_hunt && !food) ASSERT_TRUE(attacks >= 1u);
                else ASSERT_EQ(attacks, 0u);
            }
        }
        {   // food beyond water is work for the Swimmers (the island play): the ants do not go hunting for want of a pile they reach on foot
            sim::SimulationEngine sim;
            field(sim, 432);
            sim.set_player_eggs(0, 0);
            for (int y = 0; y < 60; ++y) {
                for (int x = 24; x <= 27; ++x) sim.set_terrain(x, y, sim::TERRAIN_WATER);
            }
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{12, 12});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{13, 12});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{14, 12});
            sim.spawn_unit(1, sim::AntType::Worker, TileCoord{20, 30});
            place_pile(sim, 40, 12, 400, 25, {{400, 369}, {300, 370}, {200, 371}, {100, 372}, {0, kPileGone}});
            LevelPlan plan = plan_for(Level::Hard);
            plan.opportunity = false;
            plan.assault_after = 200;
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(1400);
            ASSERT_TRUE(rig.as<StandardBot>().islands().active());
            ASSERT_FALSE(rig.as<StandardBot>().tactics().no_work);
            ASSERT_EQ(rig.proposed_count(CommandType::GroupAttack), 0u);
        }
    } TEST_END();

    TEST_CASE("AI31.7 Chores: A Ring Of Fire Walls Round An Enemy Gate Is Lit Only While That Team Has Food To Bank (A Pile Its Hill Reaches, An Ant That Carries); Without The Rule The Lone Fire Ant Lights It For Nothing")
    {
        sim::SimulationEngine probe;
        field(probe, 90);
        const MapInfo pmap(probe);
        const std::array<TileCoord, 8> ring = SabotageTask::ring_of(pmap.hill(1));
        struct Case {
            bool worth_rule;
            bool carrier;
            bool expect_lit;
        };
        const Case cases[] = {
            {false, false, true},            // (the bot as it was: the ring is lit whether or not it costs the enemy anything)
            {true, false, false},            // nothing to bank: nothing to lose
            {true, true, true},              // an ant that carries food
        };
        for (const Case& cs : cases) {
            sim::SimulationEngine sim;
            field(sim, 90);
            sim.set_player_score(1, 400);                                       // (team 1 leads and has points to lose)
            sim.spawn_unit(0, sim::AntType::Fire, TileCoord{30, 30});
            const uint32_t foe = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{46, 40});
            if (cs.carrier) sim.get_unit(foe).pick_up_food(1, 25);
            LevelPlan plan = plan_for(Level::Hard);
            without_war_batch(plan);
            plan.sabotage = true;
            plan.sabotage_safe = true;
            plan.sabotage_after = 20;
            plan.sabotage_spare_keeper = false;
            plan.opportunity = false;
            plan.chore_worth = cs.worth_rule;
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(1200);
            size_t lit = 0;
            for (const TileCoord& t : ring) lit += sim.grid().has_fire_at(t) ? 1u : 0u;
            if (cs.expect_lit) ASSERT_TRUE(lit >= 4u);
            else ASSERT_EQ(lit, 0u);
        }
        {   // the rule itself, on a view: no pile and no carrier, then an ant that carries
            sim::SimulationEngine sim;
            field(sim, 91);
            const uint32_t foe = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{46, 40});
            const MapInfo map(sim);
            ASSERT_FALSE(fire_in_worth_it(BotView::build(sim, 0, &map), map, 1));
            sim.get_unit(foe).pick_up_food(1, 25);
            tick_all(sim, 1);                                                           // (the view reads the world state that a tick makes)
            ASSERT_TRUE(fire_in_worth_it(BotView::build(sim, 0, &map), map, 1));
            ASSERT_FALSE(fire_in_worth_it(BotView::build(sim, 0, &map), map, 3));       // (a team with no ant and no pile of its own side)
        }
        {   // a pile with units left that the hill of the team walks to
            sim::SimulationEngine sim;
            field(sim, 92);
            place_pile(sim, 50, 20, 400, 25, {{400, 369}, {300, 370}, {200, 371}, {100, 372}, {0, kPileGone}});
            const MapInfo map(sim);
            ASSERT_TRUE(fire_in_worth_it(BotView::build(sim, 0, &map), map, 1));
        }
        {   // a pile that the hill does not walk to (an island in a lake), and a carrier of ANOTHER team: neither is a stake of team 1
            sim::SimulationEngine sim;
            field(sim, 93);
            for (int y = 18; y <= 24; ++y) {
                for (int x = 44; x <= 56; ++x) sim.set_terrain(x, y, sim::TERRAIN_WATER);
            }
            for (int y = 20; y <= 22; ++y) {
                for (int x = 49; x <= 51; ++x) sim.set_terrain(x, y, sim::TERRAIN_WALKABLE);
            }
            place_pile(sim, 50, 21, 400, 25, {{400, 369}, {300, 370}, {200, 371}, {100, 372}, {0, kPileGone}});
            const uint32_t other = sim.spawn_unit(2, sim::AntType::Worker, TileCoord{10, 40});
            sim.get_unit(other).pick_up_food(1, 25);
            tick_all(sim, 1);
            const MapInfo map(sim);
            ASSERT_FALSE(fire_in_worth_it(BotView::build(sim, 0, &map), map, 1));
        }
    } TEST_END();
}
