// AI58: the idle batch of the computer players (docs/BOT_IDLE.md): what their ants do when nothing is planned for them, in hand-made worlds with the real engine and in whole matches of the
// map that the rules were found on.
//
//   AI58.1  two ants face to face on a bridge: the engine leaves them standing for ever, the bot's head-on rule steps one back to land and the other crosses; with the rule off they stand
//   AI58.10 an own ant that stands idle on the one tile of a bridge's landing, with a walker stopped behind it, steps off it and the walker crosses; with the rule off it stays
//   AI58.11 an ant that waits at a strip does not stand on a bridge piece beside the drop tile (the Thieves of the strip go home over it); with the rule off it does
//   AI58.12 a Thief with loot that no walk takes home gets the chain home (a Return job); an empty-handed one waits for its raid chain
//   AI58.13 a hurt ant on the satellite walks into the hill's door over the bridge dug for it and is whole again; a whole one walks to the tile where the ants of the hill wait
//   AI58.2  one free ant beside an enemy that stands still strikes it (and not when the enemy is the stronger, when an enemy Combat Ant stands near, for a Swimmer, with the rule off)
//   AI58.3  an enemy Thief that walks to the own hole is met by the idle ants (blockers on the three tiles, a blow for the Thief): the hill keeps its points; with the rule off the raid goes through
//   AI58.4  with the food gone the idle ants hold the three tiles in front of the thief hole, and a raid of a Thief of another team ends in the can't-go clip; with the rule off it takes its 50 points
//   AI58.15 an own ant that holds food stands in the can't-go clip at the foot of a one-tile bridge with an enemy ant on the bridge tile in front of it: it steps back off the foot and the enemy crosses; with the foot rule off both stand
//   AI58.19 the same foot, an ant of team 0 and an ant of team 3 at the two feet of a bridge, each in the clip, each with a bot: only the lower team gives way; where the lower team's ant is a person's, the other gives way after 200 ticks
//   AI58.18 the same foot, the ant looked at in 36 phases of its can't-go cycle: sent off the landing in every one, by a volley of clicks where the look finds it in the clip
//   AI58.16 an ant that holds food and stands on a satellite with no walk home gets the bridge home from ONE Swimmer (the dig of the other ants asks for three) and banks its food; with the loaded rule off nobody digs
//   AI58.17 whole matches on ISLANDS: with the loaded rule on the island guard gives back the ants that hold a load for a long time with no bridge home (the bridge planner digs for them); with it off it never does
//   AI58.14 the posts at the thief hole are held while a Thief was seen lately and no food can be carried, and given up a latch after the last sighting
//   AI58.5  a hurt ant that stands idle walks home and is whole again; with the rule off it stands
//   AI58.6  whole matches on ISLANDS: with the endgame Thief on the bots take the Thieves that fall at the strips, dig the chains and steal; with it off nobody steals; the matches replay
//   AI58.7  whole matches on ISLANDS: with the Fire hats the bots take more of the Fire hats of the corner rows (the ring, 3 to 6 water tiles from a hill) and Swimmers dig the chain home for the Fire Ant; with the rule off none is dug
//   AI58.8  a whole ant that stands idle on a satellite gets the bridge home when the bridge rule is on (and the endgame wants a Thief); with it off it stays
//   AI58.9  HELLRAISER (a gate map): the island rules (the endgame Thief, the Fire hats, the bridge rule) change nothing there: the same match with them on and off
#include <algorithm>
#include <array>
#include <functional>
#include <cstdint>
#include <string>

#include "ai_test.hpp"
#include "ants_ai/arena.hpp"
#include "ants_ai/standard_bot.hpp"
#include "b41_helpers.hpp"
#include "flower_lab.hpp"

namespace {

using namespace ai_test;
using namespace ants;
using namespace ants::ai;
using namespace b41;
using flower_lab::Lab;

constexpr int kBridgeX = 30;                    // the bridge across the river: x 30, y 25 .. 29 (finished tiles), the river is the rows 25 to 29 across the whole field
constexpr int kRiverTop = 25;
constexpr int kRiverBottom = 29;

// The field of AI58.1: a river five tiles wide across the whole field and a bridge of five finished tiles over it (life for the whole test)
void river_with_bridge(sim::SimulationEngine& sim, uint32_t seed = 31) {
    empty_field(sim, seed);
    for (int y = kRiverTop; y <= kRiverBottom; ++y) {
        for (int x = 0; x < 60; ++x) sim.set_terrain(x, y, sim::TERRAIN_WATER);
        sim.grid_mut().set_bridge_at(TileCoord{kBridgeX, y}, 4, 1000000);
    }
}

LevelPlan plan_headon(bool on) {
    LevelPlan p = peace_plan(Level::Hard);
    p.bridge_headon = on;
    return p;
}

LevelPlan plan_foot(bool foot) {
    LevelPlan p = peace_plan(Level::Hard);
    p.bridge_headon = true;
    p.bridge_foot = foot;
    return p;
}

// The plan of the hand-made worlds of the idle batch with exactly one rule on or off (the others as the level plays them: the world has nothing for them to do)
LevelPlan plan_of(Level level, const std::function<void(LevelPlan&)>& tweak) {
    LevelPlan p = peace_plan(level);
    p.idle_jobs = p.thief_answer = p.contact_strike = p.bridge_headon = false;
    tweak(p);
    return p;
}

uint16_t hp_of(const sim::SimulationEngine& sim, uint32_t id) {
    const sim::AntSnapshot* a = snapshot_of(sim, id);
    return a == nullptr ? 0u : a->hp;
}

// The score of a team after a Thief of team 1 has been told to raid the hill of team 0 (its mound's entrance at (5, 5)), and the Thief
void send_raid(sim::SimulationEngine& sim, uint32_t thief) { sim.apply_command(command_of(CommandType::GroupSpecial, 1, {thief}, 5, 5)); }

}  // namespace

void run_idle_tests() {
    TEST_CASE("AI58.1 Two Ants Face To Face On A Bridge: One Walks North And One South Over A Five-Tile Bridge And Stand For Good; With The Head-On Rule On The One Nearer Land Steps Back And The Other Crosses (A Hard Bot)") {
        struct Out {
            bool north_crossed{false};
            bool south_crossed{false};
            uint32_t backoffs{0};
            uint32_t jams{0};
            uint32_t stood{0};                                                                   // ticks at which both stood on the bridge without moving
        };
        const auto play = [&](bool rule) {
            sim::SimulationEngine sim;
            river_with_bridge(sim);
            const uint32_t north = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{kBridgeX, kRiverTop - 1});     // on the north shore, bound south
            const uint32_t south = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{kBridgeX, kRiverBottom + 1});  // on the south shore, bound north
            sim.apply_command(command_of(CommandType::GroupMove, 0, {north}, kBridgeX, kRiverBottom + 4));
            sim.apply_command(command_of(CommandType::GroupMove, 0, {south}, kBridgeX, kRiverTop - 4));
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan_headon(rule)), 4, 4);
            // the owner of each ant (a task of its seat) clicks it again when it stands idle: the engine's meeting leaves both ants idle, which is what the watched match's economy and heal walk kept clicking
            const std::array<std::pair<uint32_t, TileCoord>, 2> goals = {{{north, TileCoord{kBridgeX, kRiverBottom + 4}}, {south, TileCoord{kBridgeX, kRiverTop - 4}}}};
            rig.after_sim_tick = [&]() {
                if (sim.current_tick() % 40 != 0) return;
                for (const auto& g : goals) {
                    const sim::AntSnapshot* a = snapshot_of(sim, g.first);
                    if (a != nullptr && a->state == sim::UnitState::Idle && !(a->tile_x == g.second.x && a->tile_y == g.second.y) && !sim.has_pending_path(g.first) && rig.as<StandardBot>().ledger().owner(g.first) == kNoTask) {
                        sim.apply_command(command_of(CommandType::GroupMove, 0, {g.first}, g.second.x, g.second.y));
                    }
                }
            };
            Out out;
            for (int t = 0; t < 1500; ++t) {
                rig.tick();
                const sim::AntSnapshot* n = snapshot_of(sim, north);
                const sim::AntSnapshot* s = snapshot_of(sim, south);
                if (n != nullptr && n->tile_y > kRiverBottom) out.north_crossed = true;
                if (s != nullptr && s->tile_y < kRiverTop) out.south_crossed = true;
                if (t >= 700 && n != nullptr && s != nullptr && n->tile_y >= kRiverTop && n->tile_y <= kRiverBottom && s->tile_y >= kRiverTop && s->tile_y <= kRiverBottom) ++out.stood;
            }
            out.backoffs = rig.as<StandardBot>().head_on().backoffs();
            out.jams = rig.as<StandardBot>().head_on().jams();
            return out;
        };
        const Out off = play(false);
        ASSERT_FALSE(off.north_crossed || off.south_crossed);                                    // the engine's rule: they meet in the middle and stand there
        ASSERT_TRUE(off.stood > 700);
        ASSERT_EQ(off.backoffs, 0u);
        const Out on = play(true);
        ASSERT_TRUE(on.backoffs >= 1u && on.jams >= 1u);
        ASSERT_TRUE(on.north_crossed || on.south_crossed);                                       // one of them crossed
        ASSERT_TRUE(on.stood < 200);                                                             // and the bridge was clear again soon
    } TEST_END();
    TEST_CASE("AI58.10 An Ant That Stands Idle On The Foot Of A Bridge Is In The Way: A Worker Bound North Over A Five-Tile Bridge Stops At Its End, Where Another Own Worker Stands Idle On The One Tile Of The Landing; With The Head-On Rule On The Idle One Steps Off And The Walker Crosses (A Hard Bot)") {
        struct Out {
            bool crossed{false};
            uint32_t plugs{0};
        };
        const auto play = [&](bool rule) {
            sim::SimulationEngine sim;
            river_with_bridge(sim);
            sim.set_terrain(kBridgeX - 1, kRiverTop - 1, sim::TERRAIN_WATER);                    // (the landing is one tile: the two beside it are water)
            sim.set_terrain(kBridgeX + 1, kRiverTop - 1, sim::TERRAIN_WATER);
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{kBridgeX, kRiverTop - 1});         // the plug
            const uint32_t walker = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{kBridgeX, kRiverBottom + 1});
            const TileCoord goal{kBridgeX, kRiverTop - 5};
            sim.apply_command(command_of(CommandType::GroupMove, 0, {walker}, goal.x, goal.y));
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan_headon(rule)), 4, 4);
            rig.after_sim_tick = [&]() {                                                         // (its owner clicks it again when it stands idle)
                if (sim.current_tick() % 40 != 0) return;
                const sim::AntSnapshot* a = snapshot_of(sim, walker);
                if (a != nullptr && a->state == sim::UnitState::Idle && !(a->tile_x == goal.x && a->tile_y == goal.y) && !sim.has_pending_path(walker) && rig.as<StandardBot>().ledger().owner(walker) == kNoTask) {
                    sim.apply_command(command_of(CommandType::GroupMove, 0, {walker}, goal.x, goal.y));
                }
            };
            Out out;
            for (int t = 0; t < 1500; ++t) {
                rig.tick();
                const sim::AntSnapshot* w = snapshot_of(sim, walker);
                if (w != nullptr && w->tile_y < kRiverTop - 1) out.crossed = true;
            }
            out.plugs = rig.as<StandardBot>().head_on().plugs();
            return out;
        };
        const Out off = play(false);
        ASSERT_FALSE(off.crossed);                                                               // the engine's rule: the landing is held, the walker stands on the bridge
        ASSERT_EQ(off.plugs, 0u);
        const Out on = play(true);
        ASSERT_TRUE(on.plugs >= 1u);
        ASSERT_TRUE(on.crossed);
    } TEST_END();

    TEST_CASE("AI58.11 An Ant That Waits At A Strip Does Not Stand On A Bridge Piece: Three Swimmers And A Worker (Four Swimmers Wanted), Finished Bridge Pieces On The Three Tiles West Of The Top Flower Tile; With The Endgame Thief On The Worker Waits On Another Tile, With It Off It Takes The Nearest, A Piece (ISLANDS, A Hard Bot)") {
        const TileCoord drop{30, 5};
        for (const bool rule : {true, false}) {
            Lab lab([rule](LevelPlan& p) {
                p.island_swarm_swimmers = 4;
                p.island_ferry = false;
                p.island_endraid = rule;
            }, "ISLANDS", 0x01, 0, [&](sim::SimulationEngine& sim) {
                for (int dy = -1; dy <= 1; ++dy) sim.set_bridge_at(TileCoord{drop.x - 1, drop.y + dy}, 4, 1000000);
            });
            for (int i = 0; i < 3; ++i) lab.sim.spawn_unit(0, sim::AntType::Swimmer, TileCoord{10 + i, 4});
            const uint32_t worker = lab.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{20, 6});
            lab.run(900);
            const sim::AntSnapshot* a = lab.ant(worker);
            ASSERT_TRUE(a != nullptr && lab.bot()->swarm().staged() == 1u);
            const bool on_piece = lab.sim.grid().get_cell(TileCoord{a->tile_x, a->tile_y}).has_any_bridge();
            ASSERT_EQ(on_piece, !rule);
        }
    } TEST_END();

    TEST_CASE("AI58.12 A Thief With Loot That No Walk Takes Home Gets A Chain Home: A Thief That Carries 50 Points Stands Idle On The Satellite West Of The Hill, Three Swimmers Are There To Dig, The Endgame Thief Is On; The Two Tiles Home Are Dug For It, With A Rule Off There Is No Chain For An Ant That Carries (ISLANDS, A Hard Bot)") {
        const TileCoord satellite{12, 23};
        for (const bool loaded : {true, false}) {
            Lab lab([](LevelPlan& p) {
                p.island_return = true;
                p.island_ferry = false;
                p.island_endraid = true;
                p.endraid_food = 1000000;                                                        // (the food is nearly gone, as far as the bot can tell: the late game of the island play)
                p.bridge_more = true;
            });
            std::vector<uint32_t> start_ants;
            for (const auto& a : lab.sim.get_world_state().ants) start_ants.push_back(a.id);
            for (const uint32_t id : start_ants) lab.sim.kill_unit(id);
            for (int i = 0; i < 3; ++i) lab.sim.spawn_unit(0, sim::AntType::Swimmer, TileCoord{14 + i, 19});
            const uint32_t thief = lab.sim.spawn_unit(0, sim::AntType::Thief, satellite);
            if (loaded) lab.sim.get_unit(thief).carried_points = 50;
            lab.run(2400);
            const bool dug = lab.bot()->flower_link().bridges().tiles_ordered(BridgeJob::Kind::Return) > 0u;
            ASSERT_EQ(dug, loaded);                                                              // (an empty-handed Thief waits for its raid chain, which is another job)
        }
    } TEST_END();

    TEST_CASE("AI58.2 One Free Ant Beside An Enemy That Stands Still Strikes It: A Worker Next To An Idle Enemy Worker Hits It; Not When The Enemy Has More Hit Points, When An Enemy Combat Ant Stands Near, Or With The Rule Off (A Hard Bot)") {
        struct Out {
            uint16_t enemy_hp{0};
            uint32_t started{0};
        };
        const auto play = [&](bool rule, uint16_t own_hp, uint16_t enemy_hp, bool combat_near, sim::AntType own_type = sim::AntType::Worker) {
            sim::SimulationEngine sim;
            empty_field(sim, 33);
            const uint32_t mine = sim.spawn_unit(0, own_type, TileCoord{30, 30});
            const uint32_t foe = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{31, 30});
            if (combat_near) sim.spawn_unit(1, sim::AntType::Combat, TileCoord{34, 30});
            sim.get_unit(mine).hp = own_hp;
            sim.get_unit(foe).hp = enemy_hp;
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan_of(Level::Hard, [&](LevelPlan& p) { p.contact_strike = rule; })), 4, 4);
            rig.run(500);
            Out out;
            out.enemy_hp = hp_of(sim, foe);
            out.started = rig.as<StandardBot>().contact().started();
            return out;
        };
        const Out on = play(true, 10, 10, false);
        ASSERT_TRUE(on.started >= 1u);
        ASSERT_TRUE(on.enemy_hp < 10u);                                                          // the blows landed
        const Out off = play(false, 10, 10, false);
        ASSERT_EQ(off.started, 0u);
        ASSERT_EQ(off.enemy_hp, 10u);
        const Out weaker = play(true, 7, 10, false);                                             // the own ant is the weaker: no blow
        ASSERT_EQ(weaker.started, 0u);
        ASSERT_EQ(weaker.enemy_hp, 10u);
        const Out combat = play(true, 10, 10, true);                                             // an enemy Combat Ant stands near: a person looks first
        ASSERT_EQ(combat.started, 0u);
        const Out ferry = play(true, 10, 10, false, sim::AntType::Swimmer);                      // the ferry's ant does not strike (from the water the engine refuses the blow)
        ASSERT_EQ(ferry.started, 0u);
        ASSERT_EQ(ferry.enemy_hp, 10u);
    } TEST_END();

    TEST_CASE("AI58.3 An Enemy Thief That Walks To The Own Hole Is Met By The Idle Ants: Blockers Stand On The Tiles In Front Of It, The Hill Keeps Its Points, The Thief Is Struck; With The Rule Off The Raid Goes Through (A Hard Bot, Six Idle Workers)") {
        struct Out {
            int32_t score{0};
            uint16_t thief_hp{0};
            uint32_t meets{0};
        };
        const auto play = [&](bool rule) {
            sim::SimulationEngine sim;
            empty_field(sim, 35);
            sim.set_player_score(0, 300);
            for (int i = 0; i < 6; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{10 + i % 3, 8 + i / 3});
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan_of(Level::Hard, [&](LevelPlan& p) { p.thief_answer = rule; })), 4, 4);
            rig.run(120);                                                                       // (a match is some ticks old when a Thief comes: the posts are walked to at once)
            const uint32_t thief = sim.spawn_unit(1, sim::AntType::Thief, TileCoord{19, 6});
            send_raid(sim, thief);
            rig.run(700);
            Out out;
            out.score = sim.get_player_score(0);
            out.thief_hp = hp_of(sim, thief);
            out.meets = rig.as<StandardBot>().thief_guard().meets_started();
            return out;
        };
        const Out off = play(false);
        ASSERT_EQ(off.score, 250);                                                               // the raid took its 50 points
        ASSERT_EQ(off.meets, 0u);
        const Out on = play(true);
        ASSERT_TRUE(on.meets >= 1u);
        ASSERT_EQ(on.score, 300);                                                                // the hole was shut: the Thief took nothing
        ASSERT_TRUE(on.thief_hp < 10u);                                                          // and it was struck on the tiles in front of the hole
    } TEST_END();

    TEST_CASE("AI58.4 With The Food Gone The Idle Ants Hold The Three Tiles In Front Of The Thief Hole: A Raid Of Another Team's Thief Then Ends In The Can't-Go Clip And The Hill Keeps Its Points; With The Rule Off It Takes 50 (A Hard Bot, Six Idle Workers)") {
        struct Out {
            int32_t score{0};
            size_t holders{0};
            uint32_t holds{0};
        };
        const auto play = [&](bool rule, bool gone = true, bool fire_ant = false) {
            sim::SimulationEngine sim;
            empty_field(sim, 37);
            sim.set_player_score(0, 300);
            for (int i = 0; i < 6; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{10 + i % 3, 8 + i / 3});
            if (fire_ant) sim.spawn_unit(0, sim::AntType::Fire, TileCoord{12, 12});
            LevelPlan plan = plan_for(Level::Hard);                                              // (the food-gone batch on: the empty field is a match whose food is gone)
            without_war_batch(plan);
            if (!gone) without_gone_batch(plan);
            plan.thief_answer = false;
            plan.idle_jobs = rule;
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(1500);                                                                       // the batch begins after 400 ticks, the ants have stood idle for 200 by then
            Out out;
            for (int dy = 1; dy <= 3; ++dy) {
                for (const sim::AntSnapshot& a : sim.get_world_state().ants) out.holders += (a.player_id == 0 && a.tile_x == 8 && a.tile_y == 4 + dy) ? 1u : 0u;
            }
            const uint32_t thief = sim.spawn_unit(1, sim::AntType::Thief, TileCoord{19, 6});
            send_raid(sim, thief);
            rig.run(500);
            out.score = sim.get_player_score(0);
            out.holds = rig.as<StandardBot>().idle_jobs().holds();
            return out;
        };
        const Out off = play(false);
        ASSERT_EQ(off.holders, 0u);
        ASSERT_EQ(off.score, 250);
        const Out on = play(true);
        ASSERT_EQ(on.holders, 3u);                                                               // one ant on each tile
        ASSERT_EQ(on.score, 300);
        ASSERT_TRUE(on.holds >= 3u);
        const Out early = play(true, false);                                                     // (no food gone, no Thief seen before the one at 1,500: nobody holds a post before it)
        ASSERT_EQ(early.holders, 0u);
        const Out keeper = play(true, true, true);                                               // (a Fire Ant that can walk home keeps the walls, which a holder would stand in the way of)
        ASSERT_EQ(keeper.holders, 0u);
    } TEST_END();

    TEST_CASE("AI58.14 The Posts At The Thief Hole Are Given Up When The Thief Was Seen Long Ago: With No Food To Carry And An Enemy Thief Seen (And Gone), Three Idle Ants Hold The Tiles In Front Of The Hole; A Thief Latch (3,600 Ticks) After The Last Sighting They Are The Economy's Again (A Hard Bot, The Food-Gone Batch Off)") {
        sim::SimulationEngine sim;
        empty_field(sim, 38);
        sim.set_player_score(0, 300);
        for (int i = 0; i < 6; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{10 + i % 3, 8 + i / 3});
        LevelPlan plan = plan_for(Level::Hard);
        without_war_batch(plan);
        without_gone_batch(plan);                                                                // (no food gone: only the Thief that was seen asks for the posts)
        plan.thief_answer = false;
        plan.idle_jobs = true;
        Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
        rig.run(120);
        const uint32_t thief = sim.spawn_unit(1, sim::AntType::Thief, TileCoord{19, 6});
        send_raid(sim, thief);
        rig.run(200);
        sim.kill_unit(thief);                                                                    // (seen, and gone)
        const uint64_t seen_until = sim.current_tick();
        rig.run(1500);
        ASSERT_EQ(rig.as<StandardBot>().idle_jobs().holders(), 3u);                                                                // the Thief was seen lately and nothing is to be carried: the posts are held
        while (sim.current_tick() < seen_until + 4200) rig.tick();
        ASSERT_EQ(rig.as<StandardBot>().idle_jobs().holders(), 0u);                                                                // the latch has run out: the ants are free again
    } TEST_END();

    TEST_CASE("AI58.5 A Hurt Ant That Stands Idle Walks Home And Is Whole Again: A Worker With 4 Hit Points Far From The Hill Is At The Hill's Door And Has Its 10 Hit Points Back Within 1,500 Ticks; With The Rule Off It Stands (A Medium Bot)") {
        const auto play = [&](bool rule, uint16_t hp) {
            sim::SimulationEngine sim;
            empty_field(sim, 39);
            const uint32_t ant = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{30, 30});
            sim.get_unit(ant).hp = hp;
            Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(plan_of(Level::Medium, [&](LevelPlan& p) { p.idle_jobs = rule; })), 4, 4);
            rig.run(1500);
            const sim::AntSnapshot* a = snapshot_of(sim, ant);
            return std::make_pair(hp_of(sim, ant), a != nullptr ? TileCoord{a->tile_x, a->tile_y} : TileCoord{-1, -1});
        };
        ASSERT_EQ(play(false, 4).first, 4u);
        ASSERT_EQ(play(true, 4).first, 10u);
        ASSERT_TRUE(play(true, 10).second == (TileCoord{30, 30}));                               // a whole ant has nothing to heal: it stays where it is
    } TEST_END();
    TEST_CASE("AI58.6 Whole Matches Of Four Hard Bots On ISLANDS: With The Endgame Thief The Bots Take The Thieves That Fall At The Strips, Dig The Chains To The Hills And Steal (More Than Once A Match, Two Thieves At Most A Seat); With It Off Nobody Steals; Every Match Replays") {
        assets::LevelData level;
        ASSERT_TRUE(level.load_from_file(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/ISLANDS.LVL"));
        struct Sum {
            uint32_t steals{0};
            uint32_t thieves{0};
            uint32_t raided{0};
            uint32_t bridges{0};
            uint32_t homed{0};
            bool replayed{true};
        };
        const auto play = [&](bool rule, uint32_t min_life = 500) {
            Sum sum;
            for (uint32_t seed = 1; seed <= 3; ++seed) {
                ArenaSpec spec;
                spec.level = &level;
                spec.seed = seed;
                spec.record = true;
                for (uint8_t seat = 0; seat < 4; ++seat) {
                    BotSpec b;
                    b.seat = seat;
                    b.level = Level::Hard;
                    spec.bots.push_back(b);
                }
                spec.factory = [rule, min_life](const BotSpec& b) {
                    return std::unique_ptr<Bot>(std::make_unique<StandardBot>(b.level, Style::Random, [rule, min_life](LevelPlan& plan) {
                        plan.island_endraid = rule;
                        plan.endraid_min_life = min_life;
                    }));
                };
                const ArenaResult r = play_match(spec);
                sum.replayed = sum.replayed && r.error.empty() && replay_commands(spec, r).ok;
                for (const ArenaSeatResult& seat : r.seats) {
                    sum.steals += seat.steals;
                    sum.thieves += seat.took[static_cast<size_t>(sim::AntType::Thief)];
                    sum.raided += seat.raided;
                    sum.bridges += seat.bridge_orders;
                    sum.homed += seat.idle_batch.homed;
                }
            }
            return sum;
        };
        const Sum off = play(false);
        ASSERT_EQ(off.steals, 0u);
        ASSERT_TRUE(off.replayed);
        const Sum on = play(true);
        ASSERT_TRUE(on.replayed);
        ASSERT_TRUE(on.thieves >= 6u);                                                           // three matches of four seats: the Thieves that fell were taken
        ASSERT_TRUE(on.steals >= 6u);                                                            // ... and they stole (an own Thief with a chain to a hill and a way home)
        ASSERT_TRUE(on.raided >= 300u);                                                          // ... from each other: the points that the raids took
        ASSERT_TRUE(on.bridges > off.bridges);                                                   // ... over bridges that the Swimmers dug for them
        ASSERT_TRUE(on.homed >= 3u);                                                             // ... after the Thief of a strip had walked home (it raids from its island: the loop is short)
        const Sum cautious = play(true, 1000000);                                                // a bridge that must outlive the round trip by a million ticks never does: no raid over a chain that the bots dug
        ASSERT_TRUE(cautious.steals * 2 <= on.steals);
    } TEST_END();
    TEST_CASE("AI58.7 Whole Matches Of Four Hard Bots On ISLANDS: With The Fire Hats On The Bots Take The Fire Hats Of The Corner Rows And Swimmers Dig The Chain Home For The Fire Ant; With The Rule Off No Chain Is Dug For A Fire Ant And Fewer Hats Are Taken") {
        assets::LevelData level;
        ASSERT_TRUE(level.load_from_file(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/ISLANDS.LVL"));
        struct Sum {
            uint32_t fire{0};
            uint32_t chains{0};
        };
        const auto play = [&](bool rule) {
            Sum sum;
            for (uint32_t seed = 1; seed <= 4; ++seed) {
                ArenaSpec spec;
                spec.level = &level;
                spec.seed = seed;
                for (uint8_t seat = 0; seat < 4; ++seat) {
                    BotSpec b;
                    b.seat = seat;
                    b.level = Level::Hard;
                    spec.bots.push_back(b);
                }
                spec.factory = [rule](const BotSpec& b) {
                    return std::unique_ptr<Bot>(std::make_unique<StandardBot>(b.level, Style::Random, [rule](LevelPlan& plan) { plan.fire_hats = rule; }));
                };
                const ArenaResult r = play_match(spec);
                for (const ArenaSeatResult& seat : r.seats) {
                    sum.fire += seat.took[static_cast<size_t>(sim::AntType::Fire)];
                    sum.chains += seat.idle_batch.home_tiles;
                }
            }
            return sum;
        };
        const Sum off = play(false);
        const Sum on = play(true);
        ASSERT_EQ(off.chains, 0u);
        ASSERT_TRUE(on.chains >= 3u);                                                            // a chain was dug for a Fire Ant that stood where no walk leads home
        ASSERT_TRUE(on.fire > off.fire);                                                         // and more of the hats of the rows are taken
    } TEST_END();

    TEST_CASE("AI58.8 More Bridges: Two Whole Workers That Stand Idle On The Satellite West Of The Hill Get The Two Tiles Home Within 2,400 Ticks And Both Walk Home (The Bridge Was Dug For One Of Them); With The Bridge Rule Off They Stay Where They Are (A Hard Bot, ISLANDS, Three Swimmers, The Food Nearly Gone)") {
        const TileCoord satellite{12, 23};
        const TileCoord channel[2] = {TileCoord{17, 21}, TileCoord{16, 22}};
        for (const bool on : {true, false}) {
            Lab lab([on](LevelPlan& p) {
                p.island_return = true;
                p.island_ferry = false;
                p.island_endraid = false;
                p.endraid_food = 1000000;                                                        // (the food is nearly gone, as far as the bot can tell: the late game of the island play)
                p.bridge_more = on;
            });
            std::vector<uint32_t> start_ants;                                                    // (the start ants of the satellites would be stranded ants too: only the two of the test are)
            for (const auto& a : lab.sim.get_world_state().ants) start_ants.push_back(a.id);
            for (const uint32_t id : start_ants) lab.sim.kill_unit(id);
            for (int i = 0; i < 3; ++i) lab.sim.spawn_unit(0, sim::AntType::Swimmer, TileCoord{14 + i, 19});
            const uint32_t first = lab.sim.spawn_unit(0, sim::AntType::Worker, satellite);
            const uint32_t second = lab.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{12, 25});
            lab.run(2400);
            const sim::AntSnapshot* a = lab.ant(first);
            const sim::AntSnapshot* b = lab.ant(second);
            ASSERT_TRUE(a != nullptr && b != nullptr);
            const int stands = (lab.bridge_at(channel[0]) ? 1 : 0) + (lab.bridge_at(channel[1]) ? 1 : 0);
            ASSERT_EQ(stands == 2, on);
            ASSERT_EQ(a->tile_x >= 17, on);                                                      // both walked home over the bridge (the hill's island starts at x 18)
            ASSERT_EQ(b->tile_x >= 17, on);
            if (on) {                                                                            // ... to the tile where the ants of the hill wait (a whole ant has nothing to heal), not into the door
                TileCoord wait{-1, -1};
                for (const auto& ah : lab.sim.grid().anthills()) {
                    if (ah.team_id == 0) wait = TileCoord{ah.x + 4, ah.y + 4};
                }
                ASSERT_TRUE(std::max(std::abs(a->tile_x - wait.x), std::abs(a->tile_y - wait.y)) <= 1);
                ASSERT_TRUE(std::max(std::abs(b->tile_x - wait.x), std::abs(b->tile_y - wait.y)) <= 1);
            }
            ASSERT_EQ(lab.bot()->flower_link().bridges().tiles_ordered(BridgeJob::Kind::Return) > 0u, on);
        }
    } TEST_END();

    TEST_CASE("AI58.13 A Hurt Ant On The Satellite Walks Into The Door Of The Hill Over The Bridge That Was Dug For It And Is Whole Again (4 Hit Points, Three Swimmers, ISLANDS, A Hard Bot): A Whole Ant Walks To The Tile Where The Ants Of The Hill Wait Instead, And Heals Nothing") {
        for (const bool hurt : {true, false}) {
            Lab lab([](LevelPlan& p) {
                p.island_return = true;
                p.island_ferry = false;
                p.island_endraid = false;
                p.endraid_food = 1000000;
                p.bridge_more = true;
            });
            std::vector<uint32_t> start_ants;
            for (const auto& a : lab.sim.get_world_state().ants) start_ants.push_back(a.id);
            for (const uint32_t id : start_ants) lab.sim.kill_unit(id);
            for (int i = 0; i < 3; ++i) lab.sim.spawn_unit(0, sim::AntType::Swimmer, TileCoord{14 + i, 19});
            const uint32_t ant = lab.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{12, 23});
            if (hurt) lab.sim.get_unit(ant).hp = 4;
            lab.run(2400);
            const sim::AntSnapshot* a = lab.ant(ant);
            ASSERT_TRUE(a != nullptr);
            ASSERT_EQ(a->hp == sim::AntUnit::MAX_HP, true);                                      // (the hurt one healed at the door; the whole one was whole)
            ASSERT_TRUE(a->tile_x >= 17);                                                        // ... and both are over the bridge
            if (!hurt) {                                                                         // a whole ant stands at the waiting tile: (bx + 4, by + 4) of the hill
                TileCoord wait{-1, -1};
                for (const auto& ah : lab.sim.grid().anthills()) {
                    if (ah.team_id == 0) wait = TileCoord{ah.x + 4, ah.y + 4};
                }
                ASSERT_TRUE(std::max(std::abs(a->tile_x - wait.x), std::abs(a->tile_y - wait.y)) <= 1);
            }
        }
    } TEST_END();

    TEST_CASE("AI58.9 The Island Play Does Nothing Off The Island Maps: Four Hard Bots On HELLRAISER (A Gate Map: Food Beyond Walls) Play The Same Match With The Endgame Thief, The Fire Hats And The Bridge Rule On As With Them Off (Every Seat: The Same Score, Orders And Raids)") {
        assets::LevelData level;
        ASSERT_TRUE(level.load_from_file(std::string(ANTS_SOURCE_DIR) + "/Community-Maps/HELLRAISER.lvl"));
        const auto play = [&](bool island_rules, uint32_t seed) {
            ArenaSpec spec;
            spec.level = &level;
            spec.seed = seed;
            spec.max_ticks = 12000;
            for (uint8_t seat = 0; seat < 4; ++seat) {
                BotSpec b;
                b.seat = seat;
                b.level = Level::Hard;
                spec.bots.push_back(b);
            }
            spec.factory = [island_rules](const BotSpec& b) {
                return std::unique_ptr<Bot>(std::make_unique<StandardBot>(b.level, Style::Random, [island_rules](LevelPlan& plan) {
                    plan.island_endraid = plan.fire_hats = plan.bridge_more = island_rules;
                    plan.contact_strike = plan.idle_jobs = plan.thief_answer = plan.bridge_headon = false;       // (the rules that are not the island play are the same in both)
                }));
            };
            return play_match(spec);
        };
        for (const uint32_t seed : {1u, 2u}) {
            const ArenaResult on = play(true, seed);
            const ArenaResult off = play(false, seed);
            ASSERT_TRUE(on.error.empty() && off.error.empty());
            ASSERT_EQ(on.seats.size(), off.seats.size());
            for (size_t i = 0; i < on.seats.size(); ++i) {
                ASSERT_EQ(on.seats[i].score, off.seats[i].score);
                ASSERT_EQ(on.seats[i].orders, off.seats[i].orders);
                ASSERT_EQ(on.seats[i].raided, off.seats[i].raided);
            }
        }
    } TEST_END();
    TEST_CASE("AI58.15 The Foot Of A Bridge: An Own Ant That Holds Food Shows The Can't-Go Clip At The One Tile Of A Landing While An Enemy Ant Stands On The Bridge Tile In Front Of It (Each Wants The Other's Tile); With The Foot Rule On It Steps Back Off The Landing And The Enemy Crosses; With The Rule Off Both Stand For Good") {
        struct Out {
            bool crossed{false};
            uint32_t plugs{0};
            bool looped{false};
            bool kept_load{true};
        };
        const auto play = [&](bool foot) {
            sim::SimulationEngine sim;
            river_with_bridge(sim);
            sim.set_terrain(kBridgeX - 1, kRiverTop - 1, sim::TERRAIN_WATER);                    // (the landing is one tile: the two beside it are water)
            sim.set_terrain(kBridgeX + 1, kRiverTop - 1, sim::TERRAIN_WATER);
            // the seat is the black one (its hill at the south edge): the ant that holds food stands at the north foot and wants to go south, home; the enemy (seat 0) is on the first bridge tile and wants to go north
            const uint32_t mine = sim.spawn_unit(3, sim::AntType::Worker, TileCoord{kBridgeX, kRiverTop - 1});
            const uint32_t enemy = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{kBridgeX, kRiverTop});
            sim.get_unit(mine).pick_up_food(1, 25);
            sim.apply_command(command_of(CommandType::GroupMove, 0, {enemy}, kBridgeX, kRiverTop - 6));
            sim.apply_command(command_of(CommandType::GroupMove, 3, {mine}, kBridgeX, kRiverBottom + 5));
            Rig rig(sim, 3, Level::Hard, std::make_unique<StandardBot>(plan_foot(foot)), 4, 4);
            rig.after_sim_tick = [&]() {                                                         // (the two owners click their ants again when they stand idle: the engine's meeting leaves each in the clip, then idle)
                if (sim.current_tick() % 40 != 0) return;
                const sim::AntSnapshot* a = snapshot_of(sim, mine);
                if (a != nullptr && a->state == sim::UnitState::Idle && a->tile_y < kRiverBottom && !sim.has_pending_path(mine) && rig.as<StandardBot>().ledger().owner(mine) == kNoTask) {
                    sim.apply_command(command_of(CommandType::GroupMove, 3, {mine}, kBridgeX, kRiverBottom + 5));
                }
                const sim::AntSnapshot* e = snapshot_of(sim, enemy);
                if (e != nullptr && e->state == sim::UnitState::Idle && e->tile_y > kRiverTop - 6 && !sim.has_pending_path(enemy)) {
                    sim.apply_command(command_of(CommandType::GroupMove, 0, {enemy}, kBridgeX, kRiverTop - 6));
                }
            };
            Out out;
            for (int t = 0; t < 1500; ++t) {
                rig.tick();
                const sim::AntSnapshot* e = snapshot_of(sim, enemy);
                const sim::AntSnapshot* m = snapshot_of(sim, mine);
                if (e != nullptr && e->tile_y < kRiverTop - 1) out.crossed = true;
                if (m != nullptr && m->state == sim::UnitState::CantGo) out.looped = true;
                if (m != nullptr && m->tile_y < kRiverTop && m->carried_points == 0) out.kept_load = false;                // (it keeps its food while it is on the north bank)
            }
            out.plugs = rig.as<StandardBot>().head_on().plugs();
            return out;
        };
        const Out off = play(false);
        ASSERT_FALSE(off.crossed);                                                               // the engine's rule: neither ant gives way, an ant of another team in the way is no way at all
        ASSERT_EQ(off.plugs, 0u);
        const Out on = play(true);
        ASSERT_TRUE(on.plugs >= 1u);                                                             // the ant with the food stepped off the landing (it was in the clip or idle), with its load
        ASSERT_TRUE(on.crossed);
        ASSERT_TRUE(on.kept_load);
    } TEST_END();
    TEST_CASE("AI58.18 The Foot Of A Bridge, The First Click: The Ant With The Food Is Looked At In 36 Phases Of Its Sixteen-Tick Can't-Go Cycle (Six Spacings Of The Looks, Six Reaction Times; The Clip Lasts Eight Ticks): It Is Sent Off The Landing In Every Phase, And At A Look At Which It Shows The Clip The Click Is A Volley, Which Takes Between Two Clips; With The Rule Off It Is Never Sent") {
        struct Out {
            bool plugged{false};
            bool in_clip{false};
            size_t clicks{0};
        };
        const auto play = [&](bool foot, uint32_t delay, uint32_t interval, uint32_t reaction) {
            sim::SimulationEngine sim;
            river_with_bridge(sim);
            sim.set_terrain(kBridgeX - 1, kRiverTop - 1, sim::TERRAIN_WATER);                    // (the landing is one tile: the two beside it are water)
            sim.set_terrain(kBridgeX + 1, kRiverTop - 1, sim::TERRAIN_WATER);
            const uint32_t mine = sim.spawn_unit(3, sim::AntType::Worker, TileCoord{kBridgeX, kRiverTop - 1});
            const uint32_t enemy = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{kBridgeX, kRiverTop});
            sim.get_unit(mine).pick_up_food(1, 25);
            sim.apply_command(command_of(CommandType::GroupMove, 0, {enemy}, kBridgeX, kRiverTop - 6));
            Rig rig(sim, 3, Level::Hard, std::make_unique<StandardBot>(plan_foot(foot)), interval, reaction);
            rig.after_sim_tick = [&]() {                                                         // (the ant's own errand starts `delay` ticks late: the phase of its can't-go cycle against the bot's looks)
                if (sim.current_tick() == delay) sim.apply_command(command_of(CommandType::GroupMove, 3, {mine}, kBridgeX, kRiverBottom + 5));
                if (sim.current_tick() % 40 != 0) return;
                const sim::AntSnapshot* e = snapshot_of(sim, enemy);
                if (e != nullptr && e->state == sim::UnitState::Idle && e->tile_y > kRiverTop - 6 && !sim.has_pending_path(enemy)) {
                    sim.apply_command(command_of(CommandType::GroupMove, 0, {enemy}, kBridgeX, kRiverTop - 6));
                }
            };
            Out out;
            for (int t = 0; t < 140 && !out.plugged; ++t) {
                rig.tick();
                if (rig.as<StandardBot>().head_on().plugs() == 0) continue;
                out.plugged = true;                                                              // (the look of this tick began the plug: what it saw is the world as it stands now)
                const sim::AntSnapshot* m = snapshot_of(sim, mine);
                out.in_clip = m != nullptr && m->state == sim::UnitState::CantGo;
                for (const auto& p : rig.proposed) {
                    if (p.first == sim.current_tick() && p.second.type == CommandType::GroupMove && !p.second.ants.empty() && p.second.ants[0] == mine && p.second.tile_y == kRiverTop - 2) ++out.clicks;
                }
            }
            return out;
        };
        size_t volleys = 0;
        for (uint32_t interval = 3; interval <= 8; ++interval) {                                // (the looks of the bot against the sixteen ticks of the ant's can't-go cycle: 36 phases)
            for (uint32_t reaction = 1; reaction <= 6; ++reaction) {
                const Out off = play(false, 0, interval, reaction);
                ASSERT_FALSE(off.plugged);                                                       // (a load was a reason to stand for good, before the rule)
                const Out on = play(true, 0, interval, reaction);
                ASSERT_TRUE(on.plugged);                                                         // (whatever the phase, the ant with the food is sent off the landing: after 100 ticks of standing)
                if (on.in_clip) {
                    ASSERT_TRUE(on.clicks >= 4u);                                                // (in the clip a single click is lost: the volley is the click that takes)
                    ++volleys;
                } else {
                    ASSERT_TRUE(on.clicks >= 1u);
                }
            }
        }
        ASSERT_TRUE(volleys >= 8u);                                                              // (the phases in which the bot's look found the ant in the clip: the rule acts from it)
    } TEST_END();
    TEST_CASE("AI58.19 The Foot Of A Bridge, Two Teams: An Ant Of Team 0 And An Ant Of Team 3 Stand At The Two Feet Of A Bridge, Each In The Can't-Go Clip, Each With A Bot Of Its Own: Only The Lower Team Gives Way (Both Giving Way Is Two Ants That Leave And Come Back); Where The Lower Team's Ant Is A Person's, The Bot Of Team 3 Gives Way After A Wait Of Its Own") {
        struct Out {
            uint32_t plugs0{0};
            uint32_t plugs3{0};
            uint32_t first_plug3{0};
        };
        const auto play = [&](bool bot0) {
            sim::SimulationEngine sim;
            river_with_bridge(sim);
            sim.set_terrain(kBridgeX - 1, kRiverTop - 1, sim::TERRAIN_WATER);                    // (the landings are one tile each: the two beside each are water)
            sim.set_terrain(kBridgeX + 1, kRiverTop - 1, sim::TERRAIN_WATER);
            sim.set_terrain(kBridgeX - 1, kRiverBottom + 1, sim::TERRAIN_WATER);
            sim.set_terrain(kBridgeX + 1, kRiverBottom + 1, sim::TERRAIN_WATER);
            for (int y = 9; y <= 13; ++y) {                                                      // (a pocket of rocks that no walk reaches: a click on it is refused, the ant shows the can't-go clip)
                for (int x = 9; x <= 13; ++x) {
                    if (x < 10 || x > 12 || y < 10 || y > 12) sim.set_terrain(x, y, sim::TERRAIN_OBSTACLE);
                }
            }
            const uint32_t north = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{kBridgeX, kRiverTop - 1});
            const uint32_t south = sim.spawn_unit(3, sim::AntType::Worker, TileCoord{kBridgeX, kRiverBottom + 1});
            Rig rig3(sim, 3, Level::Hard, std::make_unique<StandardBot>(plan_foot(true)), 4, 4);
            std::unique_ptr<Rig> rig0;
            if (bot0) rig0 = std::make_unique<Rig>(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan_foot(true)), 4, 4);
            const auto owner_of = [&](uint32_t ant) {
                TaskId o = kNoTask;
                if (bot0 && ant == north) o = rig0->as<StandardBot>().ledger().owner(ant);
                if (ant == south) o = rig3.as<StandardBot>().ledger().owner(ant);
                return o;
            };
            const auto hook = [&]() {                                                            // (the owners click their ants again as soon as they can take a click, unless a bot has taken the ant)
                for (const auto& e : {std::make_pair(north, uint8_t{0}), std::make_pair(south, uint8_t{3})}) {
                    const sim::AntSnapshot* a = snapshot_of(sim, e.first);
                    if (a != nullptr && a->state == sim::UnitState::Idle && !sim.has_pending_path(e.first) && owner_of(e.first) == kNoTask) {
                        sim.apply_command(command_of(CommandType::GroupMove, e.second, {e.first}, 11, 11));
                    }
                }
            };
            Out out;
            for (int t = 0; t < 320; ++t) {
                sim.tick();
                hook();
                if (rig0) rig0->follow();
                rig3.follow();
                if (out.first_plug3 == 0 && rig3.as<StandardBot>().head_on().plugs() > 0) out.first_plug3 = static_cast<uint32_t>(t) + 1u;
            }
            out.plugs0 = rig0 ? rig0->as<StandardBot>().head_on().plugs() : 0u;
            out.plugs3 = rig3.as<StandardBot>().head_on().plugs();
            return out;
        };
        const Out both = play(true);
        ASSERT_TRUE(both.plugs0 >= 1u);                                                          // (the lower team gives way ...)
        ASSERT_EQ(both.plugs3, 0u);                                                              // (... and the other stays where it stands)
        const Out person = play(false);
        ASSERT_TRUE(person.plugs3 >= 1u);                                                        // (nobody steps back for it: after 200 ticks of standing it gives way itself)
        ASSERT_TRUE(person.first_plug3 >= 190u);
    } TEST_END();
    TEST_CASE("AI58.16 An Ant That Holds Food And Stands On The Satellite With No Walk Home Gets The Bridge From ONE Swimmer (The Dig For The Other Ants Needs Three: The Ferry's) And Banks Its Food; With The Loaded Rule Off Nobody Digs And The Food Stays Where It Is (ISLANDS, A Hard Bot)") {
        for (const bool rule : {true, false}) {
            Lab lab([rule](LevelPlan& p) {
                p.island_return = true;
                p.island_ferry = false;
                p.island_endraid = false;
                p.bridge_loaded = rule;
                p.bridge_hold_release = 600;
            });
            std::vector<uint32_t> start_ants;
            for (const auto& a : lab.sim.get_world_state().ants) start_ants.push_back(a.id);
            for (const uint32_t id : start_ants) lab.sim.kill_unit(id);
            lab.sim.spawn_unit(0, sim::AntType::Swimmer, TileCoord{14, 19});                     // ONE Swimmer: fewer than the ferry's three
            const uint32_t ant = lab.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{12, 23});
            lab.sim.get_unit(ant).pick_up_food(1, 25);
            lab.run(3600);
            const sim::AntSnapshot* a = lab.ant(ant);
            const uint32_t tiles = lab.bot()->flower_link().bridges().tiles_ordered(BridgeJob::Kind::Return);
            if (rule) {
                ASSERT_TRUE(tiles >= 1u);                                                        // a chain was dug for it
                ASSERT_TRUE(lab.sim.get_world_state().player_scores[0] >= 25);                   // ... and its food is in the hill
            } else {
                ASSERT_EQ(tiles, 0u);
                ASSERT_TRUE(a != nullptr && a->tile_x < 17 && a->is_holding);                    // (it stands where it stood, with its load)
                ASSERT_EQ(lab.sim.get_world_state().player_scores[0], 0);
            }
        }
    } TEST_END();

    TEST_CASE("AI58.17 Whole Matches Of Four Hard Bots On ISLANDS: The Island Guard Gives Back The Ants That Hold A Load For 600 Ticks With No Bridge Home When The Loaded Rule Is On (The Bridge Planner Digs For Them), And Never With It Off; Every Match Replays") {
        assets::LevelData level;
        ASSERT_TRUE(level.load_from_file(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/ISLANDS.LVL"));
        uint32_t released[2] = {0, 0};
        for (const int rule : {1, 0}) {
            for (const uint32_t seed : {12u, 16u}) {                                           // (two matches in which a guard held a load for 600 ticks with no bridge home: the arena's seeds 12 and 16)
                ArenaSpec spec;
                spec.level = &level;
                spec.seed = seed;
                spec.record = rule == 1;
                for (uint8_t seat = 0; seat < 4; ++seat) {
                    BotSpec b;
                    b.seat = seat;
                    b.level = Level::Hard;
                    spec.bots.push_back(b);
                }
                spec.factory = [rule](const BotSpec& b) {
                    return std::unique_ptr<Bot>(std::make_unique<StandardBot>(b.level, Style::Random, [rule](LevelPlan& plan) {
                        plan.bridge_loaded = rule == 1;
                        plan.strip_fight = false;                                                    // (the fight at a strip is another rule: the two matches whose guard held a load are those of the plan before it)
                    }));
                };
                const ArenaResult r = play_match(spec);
                ASSERT_TRUE(r.error.empty());
                if (spec.record) ASSERT_TRUE(replay_commands(spec, r).ok);
                for (const ArenaSeatResult& seat : r.seats) released[rule == 1 ? 0 : 1] += seat.fight_batch.loads_released;
            }
        }
        ASSERT_TRUE(released[0] >= 2u);
        ASSERT_EQ(released[1], 0u);
    } TEST_END();
}
