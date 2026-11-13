// Hazards on another hill's ground (docs/BOT_FIGHT.md, "Hazards on another hill's ground"; LevelPlan::own_ground_counters, hazard_ground in src/ants_ai/tactics.cpp, harms_economy, BombTask and WallTask in
// src/ants_ai/standard_tasks_build.cpp). The counters clear an enemy's bomb or fire wall that is near the bot's hill or a pile that its hill reaches. A hazard in the base of a team that shut itself in is none of the
// bot's business: the bot clears an enemy's hazard on its own ground, or beside a pile it has a claim to or works, and nowhere else. The scenes run the real path (a real BotView, a real BombTask and WallTask in
// a Rig on a field of grass of 60 by 60 tiles with the bot's hill at (4, 4)); each is played with the rule off and on, so that the scene shows the old trips as well as the new quiet.
//
//   AI40.1  the plans and the numbers: the rule is on at Medium and Hard, off at Easy and in a plan made by hand; the five numbers; the tune keys' defaults
//   AI40.2  hazard_ground as a function: own ground by distance and by walking cost, another hill's hole tiles and gate ring always foreign, a pile with a claim, a pile that an ant works, the ally's hill
//   AI40.3  a bomb near the enemy's hill and far from the bot's is left alone, the same bomb near the bot's hill is defused
//   AI40.4  a bomb beside a pile near the enemy's hill is left alone, beside a pile that the bot has a claim to it is defused, but not by the last ants
//   AI40.5  a fire wall on the enemy's hole tile near a pile is left alone, one near the bot's hill is put out, and the keeper of the walls stays inside its leash while walls are wanted
//   AI40.6  the worker's way (no Bomber): only on the own ground and not as one of the last ants; a Bomber needs five hit points; the map's own bombs are asked as they were
//   AI40.7  the bench bot turtle on TREASURE against three Hard bots: before, every bot goes into its base now and then to defuse or put out fire; after, none does
// AI40.1 is a pin of the plans; the others play scenes.
#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <vector>

#include "ai_test.hpp"
#include "ants_ai/arena.hpp"
#include "ants_ai/tactics.hpp"
#include "b41_helpers.hpp"
#include "bench_aggressor.hpp"

using namespace ai_test;
using namespace ants;
using namespace ants::ai;
using namespace b41;

namespace {

// The plan of the scenes: Hard's without the war batch, the rule on or off
LevelPlan scene_plan(bool own_ground) {
    LevelPlan p = peace_plan(Level::Hard);
    p.own_ground_counters = own_ground;
    return p;
}

// A field of grass with the bot's hill (team 0) at (4, 4) and the enemy's (team 1) at `enemy`; nothing else
void close_field(sim::SimulationEngine& sim, TileCoord enemy, uint32_t seed = 41) {
    sim.init_test_world(60, 60, seed, 14400 * sim::TICK_MS);
    sim.grid_mut().set_anthill(0, kFightHills[0]);
    sim.grid_mut().set_anthill(1, enemy);
}

// The bot's ants: `workers` workers round (8, 9) and, with `type`, one ant of that type
void seat_ants(sim::SimulationEngine& sim, int workers, sim::AntType type = sim::AntType::Worker) {
    for (int i = 0; i < workers; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{8 + i, 9});
    if (type != sim::AntType::Worker) sim.spawn_unit(0, type, TileCoord{8, 11});
}

// The orders (a special order or a click) that the rig's bot gave onto a tile
size_t orders_onto(const Rig& rig, TileCoord tile) {
    size_t n = 0;
    for (const auto& e : rig.proposed) {
        if ((e.second.type == CommandType::GroupSpecial || e.second.type == CommandType::GroupMove) && e.second.tile_x == tile.x && e.second.tile_y == tile.y) ++n;
    }
    return n;
}

// The scene: the bot of `plan` plays `ticks` ticks; returns the orders that named the hazard's tile
size_t play(sim::SimulationEngine& sim, const LevelPlan& plan, TileCoord hazard, uint64_t ticks) {
    Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
    rig.run(ticks);
    return orders_onto(rig, hazard);
}

}  // namespace

void run_ownground_tests() {
    TEST_CASE("AI40.1 The Plans: Counters On The Own Ground Only At Medium And Hard (Not At Easy, Not In A Plan Made By Hand), And The Numbers Of The Rule") {
        ASSERT_TRUE(plan_for(Level::Medium).own_ground_counters);
        ASSERT_TRUE(plan_for(Level::Hard).own_ground_counters);
        ASSERT_FALSE(plan_for(Level::Easy).own_ground_counters);
        const LevelPlan none;
        ASSERT_FALSE(none.own_ground_counters);
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            const LevelPlan p = plan_for(level);
            ASSERT_EQ(p.counter_home_radius, 6);
            ASSERT_EQ(p.counter_claim_percent, 170u);
            ASSERT_EQ(p.counter_work_tiles, 4);
            ASSERT_EQ(p.counter_keeper_leash, 12);
            ASSERT_EQ(p.counter_bomber_hp, 5u);
            ASSERT_EQ(p.counter_hill_radius, 10);                                                          // (the radii of the old rule are as they were)
            ASSERT_EQ(p.counter_pile_radius, 3);
        }
        for (const Style style : {Style::Aggressive, Style::Raider}) {                                      // a styled bot of the registry plays the rule as well
            StandardBot bot(Level::Hard, style);
            ASSERT_TRUE(bot.tactics().plan.own_ground_counters);
        }
    } TEST_END();

    TEST_CASE("AI40.2 hazard_ground: The Own Ground By Distance And By Walking Cost, Another Hill's Hole Tiles And Gate Ring Always Foreign, A Pile With A Claim, A Pile An Ant Works, The Ally's Hill") {
        const LevelPlan plan = scene_plan(true);
        const auto ground = [&](sim::SimulationEngine& sim, TileCoord tile) {
            const MapInfo map(sim);
            const BotView view = BotView::build(sim, 0, &map);
            return hazard_ground(view, map, plan, tile);
        };
        {   // two hills 10 tiles apart: (4, 4) and (14, 4)
            sim::SimulationEngine sim;
            close_field(sim, TileCoord{14, 4});
            seat_ants(sim, 1);
            ASSERT_TRUE(ground(sim, TileCoord{9, 8}) == HazardGround::Own);                                 // 5 tiles from the bot's hill
            ASSERT_TRUE(ground(sim, TileCoord{10, 6}) == HazardGround::Own);                                // 6 tiles: still its home ground, though the bot's walk is not shorter than the enemy's from here
            ASSERT_TRUE(ground(sim, TileCoord{11, 8}) == HazardGround::Foreign);                            // 7 tiles from the bot's hill, 3 from the enemy's: nearer to the enemy
            ASSERT_TRUE(ground(sim, TileCoord{18, 5}) == HazardGround::Foreign);                            // the enemy's thief hole (three tiles east of its hill) ...
            ASSERT_TRUE(ground(sim, TileCoord{18, 7}) == HazardGround::Foreign);
            ASSERT_TRUE(ground(sim, TileCoord{15, 2}) == HazardGround::Foreign);                            // ... and the ring round its gate
            ASSERT_TRUE(ground(sim, TileCoord{5, 2}) == HazardGround::Own);                                 // the same tile of the bot's own hill: home ground
            ASSERT_TRUE(ground(sim, TileCoord{4, 25}) == HazardGround::Own);                                // far from both but nearer to the bot: its ground too
            ASSERT_TRUE(ground(sim, TileCoord{24, 25}) == HazardGround::Foreign);                           // nearer to the enemy's hill
        }
        {   // a hill of the enemy at (6, 12): its gate ring lies within six tiles of the bot's hill and is still the enemy's
            sim::SimulationEngine sim;
            close_field(sim, TileCoord{6, 12});
            seat_ants(sim, 1);
            ASSERT_TRUE(ground(sim, TileCoord{7, 10}) == HazardGround::Foreign);
            ASSERT_TRUE(ground(sim, TileCoord{9, 9}) == HazardGround::Own);
        }
        {   // a hill of the enemy at (1, 8) beside the bot's: the first tile of its thief hole, (5, 9), lies within six tiles of the bot's hill and is still the enemy's
            sim::SimulationEngine sim;
            close_field(sim, TileCoord{1, 8});
            seat_ants(sim, 1);
            ASSERT_TRUE(ground(sim, TileCoord{5, 9}) == HazardGround::Foreign);
            ASSERT_TRUE(ground(sim, TileCoord{8, 9}) == HazardGround::Own);
        }
        {   // the ally's hill is no rival's: a tile beside it is ground that nobody else walks to for less
            sim::SimulationEngine sim;
            close_field(sim, TileCoord{14, 4});
            sim.form_alliance(0, 1);
            seat_ants(sim, 1);
            ASSERT_TRUE(ground(sim, TileCoord{11, 8}) == HazardGround::Own);
        }
        {   // piles: one beside the enemy's hill (far for the bot), one in the middle (as far for the bot as for the enemy), one beside the bot's hill
            sim::SimulationEngine sim;
            close_field(sim, TileCoord{50, 4});
            add_pile(sim, 46, 9, 60, 25);
            add_pile(sim, 27, 9, 60, 25);
            seat_ants(sim, 1);
            ASSERT_TRUE(ground(sim, TileCoord{46, 12}) == HazardGround::Foreign);                           // beside the enemy's pile: no claim
            ASSERT_TRUE(ground(sim, TileCoord{27, 12}) == HazardGround::Own);                               // beside the middle pile: no hill walks there for less than the bot's
            ASSERT_TRUE(ground(sim, TileCoord{30, 20}) == HazardGround::Foreign);                           // beside no pile, and nearer to the enemy's hill
            add_pile(sim, 32, 9, 60, 25);                                                                   // a pile that the enemy reaches first, but the bot's walk costs less than 170 percent of its
            const MapInfo map(sim);
            const BotView view = BotView::build(sim, 0, &map);
            ASSERT_TRUE(hazard_ground(view, map, plan, TileCoord{32, 12}) == HazardGround::Claimed);
            ASSERT_TRUE(hazard_ground(view, map, plan, TileCoord{34, 12}) == HazardGround::Claimed);
        }
        for (const bool beside : {true, false}) {   // an ant of the bot that carries food beside the far pile: the bot works it now (one that is far from it does not)
            sim::SimulationEngine sim;
            close_field(sim, TileCoord{50, 4});
            add_pile(sim, 46, 9, 60, 25);
            const uint32_t ant = sim.spawn_unit(0, sim::AntType::Worker, beside ? TileCoord{44, 10} : TileCoord{30, 30});
            ASSERT_TRUE(ground(sim, TileCoord{46, 12}) == HazardGround::Foreign);
            sim.get_unit(ant).pick_up_food(1, 25);
            sim.tick();                                                                                     // (the view reads the world state of the last tick)
            ASSERT_TRUE(ground(sim, TileCoord{46, 12}) == (beside ? HazardGround::Claimed : HazardGround::Foreign));
        }
    } TEST_END();

    TEST_CASE("AI40.3 An Enemy Bomb Near The Enemy's Hill And Far From The Bot's Is Left Alone (The Bomber Stays), The Same Bomb Near The Bot's Hill Is Defused; The Old Rule Went For Both") {
        const TileCoord far{11, 8};                                                                         // 7 tiles from the bot's hill (within the old radius of 10), 3 from the enemy's at (14, 4)
        const TileCoord near{9, 8};                                                                         // 5 tiles from the bot's hill
        for (const bool rule : {false, true}) {
            sim::SimulationEngine sim;
            close_field(sim, TileCoord{14, 4});
            seat_ants(sim, 4, sim::AntType::Bomber);
            sim.grid_mut().place_bomb(static_cast<uint32_t>(far.x), static_cast<uint32_t>(far.y), 1);
            const size_t orders = play(sim, scene_plan(rule), far, 600);
            if (rule) {
                ASSERT_EQ(orders, 0u);
                ASSERT_TRUE(sim.grid().has_bomb_at(far));
            } else {
                ASSERT_TRUE(orders >= 1);                                                                  // (the old rule: the Bomber walks into the enemy's base)
                ASSERT_FALSE(sim.grid().has_bomb_at(far));
            }
            sim::SimulationEngine home;
            close_field(home, TileCoord{14, 4});
            seat_ants(home, 4, sim::AntType::Bomber);
            home.grid_mut().place_bomb(static_cast<uint32_t>(near.x), static_cast<uint32_t>(near.y), 1);
            ASSERT_TRUE(play(home, scene_plan(rule), near, 600) >= 1);
            ASSERT_FALSE(home.grid().has_bomb_at(near));
        }
    } TEST_END();

    TEST_CASE("AI40.4 An Enemy Bomb Beside A Pile Near The Enemy's Hill Is Left Alone, Beside A Pile It Has A Claim To It Is Defused (Not By The Last Ants)") {
        const TileCoord beside_theirs{46, 12};                                                              // 3 tiles from a pile beside the enemy's hill at (50, 4)
        const TileCoord beside_middle{32, 12};                                                              // beside a pile that the enemy reaches first, with a claim for the bot (not its own ground)
        for (const bool rule : {false, true}) {
            const auto scene = [&](TileCoord bomb, int32_t pile_col) {
                sim::SimulationEngine sim;
                close_field(sim, TileCoord{50, 4});
                add_pile(sim, 10, 12, 200, 25);                                                            // (food at home keeps the workers there)
                add_pile(sim, pile_col, 9, 60, 25);
                seat_ants(sim, 5, sim::AntType::Bomber);
                sim.grid_mut().place_bomb(static_cast<uint32_t>(bomb.x), static_cast<uint32_t>(bomb.y), 1);
                const size_t orders = play(sim, scene_plan(rule), bomb, 1800);
                return std::make_pair(orders, sim.grid().has_bomb_at(bomb));
            };
            const auto theirs = scene(beside_theirs, 46);
            if (rule) {
                ASSERT_EQ(theirs.first, 0u);
                ASSERT_TRUE(theirs.second);
            } else {
                ASSERT_TRUE(theirs.first >= 1);
                ASSERT_FALSE(theirs.second);
            }
            const auto middle = scene(beside_middle, 32);
            ASSERT_TRUE(middle.first >= 1);
            ASSERT_FALSE(middle.second);
        }
        for (const bool rule : {false, true}) {   // the same bomb with only the last ants left (the Bomber and one worker): the new rule keeps them off ground that is not their own
            sim::SimulationEngine sim;
            close_field(sim, TileCoord{50, 4});
            add_pile(sim, 10, 12, 200, 25);
            add_pile(sim, 32, 9, 60, 25);
            seat_ants(sim, 1, sim::AntType::Bomber);
            sim.grid_mut().place_bomb(static_cast<uint32_t>(beside_middle.x), static_cast<uint32_t>(beside_middle.y), 1);
            ASSERT_EQ(play(sim, scene_plan(rule), beside_middle, 1800) >= 1, !rule);
        }
    } TEST_END();

    TEST_CASE("AI40.5 An Enemy Fire Wall On The Enemy's Hole Tile Near A Pile Is Left Alone (The Fire Ant Stays), One Near The Bot's Hill Is Put Out, The Last Ants Stay Off Ground That Is Not Their Own, And While Walls Are Wanted The Keeper Does Not Leave For A Wall Beyond Its Leash") {
        const TileCoord hole{54, 6};                                                                        // one of the three tiles in front of the enemy's thief hole (the hill at (50, 4)), 4 tiles from its pile
        const TileCoord home{10, 6};                                                                        // 6 tiles from the bot's hill
        for (const bool rule : {false, true}) {
            sim::SimulationEngine sim;
            close_field(sim, TileCoord{50, 4});
            add_pile(sim, 10, 12, 200, 25);
            add_pile(sim, 52, 9, 60, 25);
            seat_ants(sim, 5, sim::AntType::Fire);
            sim.set_fire_at(hole, 3500);
            const size_t orders = play(sim, scene_plan(rule), hole, 1500);
            if (rule) {
                ASSERT_EQ(orders, 0u);
                ASSERT_TRUE(sim.grid().has_fire_at(hole));
            } else {
                ASSERT_TRUE(orders >= 1);
                ASSERT_FALSE(sim.grid().has_fire_at(hole));
            }
            sim::SimulationEngine own;
            close_field(own, TileCoord{50, 4});
            add_pile(own, 12, 12, 200, 25);
            seat_ants(own, 5, sim::AntType::Fire);
            own.set_fire_at(home, 3500);
            ASSERT_TRUE(play(own, scene_plan(rule), home, 1500) >= 1);
            ASSERT_FALSE(own.grid().has_fire_at(home));
        }
        for (const bool rule : {false, true}) {   // a wall beside a pile with a claim, with only the last ants left (the Fire Ant and one worker): put out by the old rule, left alone by the new
            sim::SimulationEngine sim;
            close_field(sim, TileCoord{50, 4});
            add_pile(sim, 10, 12, 200, 25);
            add_pile(sim, 32, 9, 60, 25);
            seat_ants(sim, 1, sim::AntType::Fire);
            sim.set_fire_at(TileCoord{32, 13}, 3500);
            LevelPlan plan = scene_plan(rule);
            plan.wall_trigger = WallTrigger::Never;
            ASSERT_EQ(play(sim, plan, TileCoord{32, 13}, 1500) >= 1, !rule);
        }
        {   // the keeper's leash: a wall beside the middle pile (a claim) is put out when no walls are wanted, and left while a thief threatens (a Thief power-up that the enemy can reach) and the walls are wanted
            const TileCoord wall{32, 13};
            for (const bool wanted : {false, true}) {
                sim::SimulationEngine sim;
                close_field(sim, TileCoord{50, 4});
                add_pile(sim, 10, 12, 200, 25);
                add_pile(sim, 32, 9, 60, 25);
                if (wanted) sim.grid_mut().place_powerup(40, 30, 3);
                seat_ants(sim, 5, sim::AntType::Fire);
                sim.set_fire_at(wall, 3500);
                LevelPlan plan = scene_plan(true);
                if (!wanted) plan.wall_trigger = WallTrigger::Never;
                const size_t orders = play(sim, plan, wall, 1500);
                if (wanted) {
                    ASSERT_EQ(orders, 0u);
                    ASSERT_TRUE(sim.grid().has_fire_at(wall));
                } else {
                    ASSERT_TRUE(orders >= 1);
                    ASSERT_FALSE(sim.grid().has_fire_at(wall));
                }
            }
        }
    } TEST_END();

    TEST_CASE("AI40.6 A Worker Steps On A Bomb On Purpose Only On The Own Ground And Not As The Last Ant; A Bomber Goes With Five Hit Points, Not Four; The Map's Own Bombs Are Asked As They Were") {
        const TileCoord far{11, 8};
        const TileCoord near{9, 8};
        for (const bool rule : {false, true}) {
            {   // no Bomber: a bomb near the enemy's hill is stepped on by an old bot, left alone by the new
                sim::SimulationEngine sim;
                close_field(sim, TileCoord{14, 4});
                seat_ants(sim, 6);
                sim.grid_mut().place_bomb(static_cast<uint32_t>(far.x), static_cast<uint32_t>(far.y), 1);
                const size_t orders = play(sim, scene_plan(rule), far, 800);
                ASSERT_EQ(orders >= 1, !rule);
                ASSERT_EQ(sim.grid().has_bomb_at(far), rule);
            }
            {   // a bomb near the bot's hill: stepped on with two workers, left alone with one (no pile is left to bank at, so only the last ant is kept: Tactics::reserve_of) under the new rule only
                for (const int workers : {1, 2}) {
                    sim::SimulationEngine sim;
                    close_field(sim, TileCoord{14, 4});
                    seat_ants(sim, workers);
                    sim.grid_mut().place_bomb(static_cast<uint32_t>(near.x), static_cast<uint32_t>(near.y), 1);
                    const size_t orders = play(sim, scene_plan(rule), near, 800);
                    ASSERT_EQ(orders >= 1, !rule || workers == 2);
                }
            }
        }
        for (const bool rule : {false, true}) {   // no Bomber, a bomb beside a pile that the bot has a claim to but not on its own ground: the old rule sends a worker, the new one does not (a claim is for the Bomber)
            sim::SimulationEngine sim;
            close_field(sim, TileCoord{14, 4});
            add_pile(sim, 13, 12, 1, 25);                                                                   // (one bite: one worker harvests, the others stand idle)
            seat_ants(sim, 6);
            const TileCoord claimed{12, 9};                                                                 // 8 tiles from the bot's hill, nearer to the enemy's by the walk, 3 from the pile
            sim.grid_mut().place_bomb(static_cast<uint32_t>(claimed.x), static_cast<uint32_t>(claimed.y), 1);
            ASSERT_EQ(play(sim, scene_plan(rule), claimed, 800) >= 1, !rule);
        }
        {   // a Bomber with four hit points does not go (a second bomb on its way would kill it); with five it does
            for (const uint16_t hp : std::initializer_list<uint16_t>{4, 5}) {
                sim::SimulationEngine sim;
                close_field(sim, TileCoord{14, 4});
                seat_ants(sim, 4);
                const uint32_t bomber = sim.spawn_unit(0, sim::AntType::Bomber, TileCoord{8, 11});
                sim.get_unit(bomber).hp = hp;
                sim.grid_mut().place_bomb(static_cast<uint32_t>(near.x), static_cast<uint32_t>(near.y), 1);
                LevelPlan plan = scene_plan(true);
                plan.bomb_hit = false;                                                                      // (no worker takes its place)
                const size_t orders = play(sim, plan, near, 600);
                ASSERT_EQ(orders >= 1, hp == 5);
            }
        }
        {   // the map's own bomb (no team) near the enemy's hill and within the old radius of the bot's hill: asked as it always was, the Bomber goes
            sim::SimulationEngine sim;
            close_field(sim, TileCoord{14, 4});
            seat_ants(sim, 4, sim::AntType::Bomber);
            sim.grid_mut().place_bomb(static_cast<uint32_t>(far.x), static_cast<uint32_t>(far.y), 255);
            ASSERT_TRUE(play(sim, scene_plan(true), far, 600) >= 1);
        }
    } TEST_END();

    TEST_CASE("AI40.7 The Bench Bot Turtle (Walls, Bombs, Its Own Piles, Never Leaves) On TREASURE Against Three Hard Bots: The Old Rule Sends Every Bot Into Its Base To Defuse And Put Out Fire, The New Rule Sends None") {
        ASSERT_FALSE(known_bot_kind("turtle"));
        ASSERT_EQ(bench::bench_kind_of("TURTLE"), std::string("turtle"));
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) ASSERT_EQ(std::string(bench::make_bench_bot("turtle", level)->kind()), std::string("turtle"));
        struct Tally {
            uint32_t trips{0};              // defusals and extinguishings of the three bots begun on ground that lies nearer the turtle's hill than their own
            uint32_t bombs{0};              // bombs the turtle planted
            uint32_t fires{0};              // fire walls the turtle lit
            uint32_t turtle_scored{0};
            uint32_t bots_banked{0};
        };
        const auto match = [](uint32_t seed, bool rule, Tally& tally) {
            ArenaSpec spec;
            spec.level = &level_of("TREASURE");
            spec.seed = seed;
            spec.max_ticks = 7200;
            for (uint8_t seat = 0; seat < 4; ++seat) {
                BotSpec b;
                b.seat = seat;
                b.kind = seat == 0 ? "turtle" : "standard";
                b.level = Level::Hard;
                spec.bots.push_back(b);
            }
            spec.extra_kinds = {"turtle"};
            spec.factory = [rule](const BotSpec& b) -> std::unique_ptr<Bot> {
                if (b.kind == "turtle") return bench::make_bench_bot(b.kind, b.level);
                return std::make_unique<StandardBot>(b.level, Style::Random, [rule](LevelPlan& p) { p.own_ground_counters = rule; });
            };
            std::vector<uint8_t> was_busy;                                                                  // by ant id: the ant was defusing or putting out fire at the last tick
            sim::TileCoord hill0{-1, -1};
            std::vector<sim::TileCoord> hills(4, sim::TileCoord{-1, -1});
            spec.on_tick = [&](const sim::SimulationEngine& engine) {
                const sim::WorldState& ws = engine.get_world_state();
                if (hills[0].x < 0) {
                    for (const assets::AnthillSpawn& h : engine.grid().anthills()) {
                        if (h.team_id < 4) hills[h.team_id] = sim::TileCoord{h.x, h.y};
                    }
                    hill0 = hills[0];
                }
                for (const sim::AntSnapshot& a : ws.ants) {
                    if (a.id >= was_busy.size()) was_busy.resize(static_cast<size_t>(a.id) + 64, 0);
                    const bool busy = a.state == sim::UnitState::DefusingBomb || a.state == sim::UnitState::ExtinguishingFire;
                    if (busy && was_busy[a.id] == 0 && a.player_id != 0 && a.player_id < 4) {
                        const sim::TileCoord here{a.tile_x, a.tile_y};
                        const auto cheb = [](sim::TileCoord p, sim::TileCoord q) { return std::max(std::abs(p.x - q.x), std::abs(p.y - q.y)); };
                        if (cheb(here, hill0) < cheb(here, hills[a.player_id])) ++tally.trips;
                    }
                    was_busy[a.id] = busy ? 1 : 0;
                }
            };
            const ArenaResult r = play_match(spec);
            ASSERT_TRUE(r.error.empty());
            ASSERT_EQ(r.seats[0].runs, std::string("turtle"));
            tally.bombs = r.seats[0].bombs_planted;
            tally.fires = r.seats[0].fires_lit;
            tally.turtle_scored = static_cast<uint32_t>(std::max(0, r.seats[0].score));
            for (size_t s = 1; s < r.seats.size(); ++s) tally.bots_banked += r.seats[s].banked;
        };
        uint32_t old_trips = 0;
        uint32_t new_trips = 0;
        for (const uint32_t seed : {1u, 2u, 3u, 4u}) {
            Tally before;
            Tally after;
            match(seed, false, before);
            match(seed, true, after);
            ASSERT_TRUE(before.bombs >= 3 && before.fires >= 3);                                            // the turtle walls and mines its base (the same in both: it does not look at the bots' rule)
            ASSERT_TRUE(before.turtle_scored > 0);                                                          // and it harvests
            old_trips += before.trips;
            new_trips += after.trips;
            ASSERT_EQ(after.trips, 0u);
        }
        ASSERT_TRUE(old_trips >= 4);                                                                        // the old rule went into the base: in four matches of the three bots, at least four trips
        ASSERT_EQ(new_trips, 0u);
    } TEST_END();
}
