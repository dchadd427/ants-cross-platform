// AI57: the computer players that think they are stuck solve it (docs/BOT_UNSTUCK.md): the plug task (a power-up that shuts a way is taken by one ant), the refusal memory, the rescue of a walled-in Thief
// and the stuck tally of the arena, in hand-made worlds with the real engine, and whole matches of the maps that the rules were found on.
//
//   AI57.1  the engine's loop: a thief that has raided and has no walk home stands on the raid tile in the can't-go clip, saying it again and again, and the loop never ends by itself
//   AI57.2  a power-up that plugs the only gap of a line of rocks is taken by the stranded ant, and the ant of the far part has a walk home; with plan.plugs off it stays plugged
//   AI57.3  a plug that opens nothing for the seat (no own ant and no pile behind it) is left where it is
//   AI57.4  a pile behind a plug that no ant stands behind: the nearest ant of the hill's part is sent for the power-up; a typed ant (a Thief) behind it is not (it would lose its type)
//   AI57.5  the plug task and the trip task do not send two ants to one power-up
//   AI57.6  the refusal memory: a click that the engine refuses twice is blocked, longer after the third, and the first refusal blocks nothing; a carrier at the gate is never judged
//   AI57.7  the stuck tally counts a thief in the can't-go loop and a carrier with no order once the span is 10 seconds old, an ant at work never, and play_match reports it
//   AI57.8  a Thief that raided and was walled in on the raid tile is freed by an own Fire Ant, which puts a wall of the hole out; a wall that shuts in nobody is not its business; with the rule off the thief stays
//   AI57.9  whole matches: on the map of the power-ups that plug the ways the bots strand fewer ants and refuse fewer orders than the bots without the rules (unstick=0), and the match replays
//   AI57.10 the boundary click: a Thief in the engine's can't-go loop with a pocket of its own is clicked onto an ordinary tile at the one tick of the cycle at which the engine takes it (rules number 14), and sent home
//           when a walk opens; under the rules before 14, with the rule off, or at Easy the loop goes on
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

#include "ai_test.hpp"
#include "ants_ai/arena.hpp"
#include "ants_ai/refusal_memory.hpp"
#include "ants_ai/standard_bot.hpp"
#include "b41_helpers.hpp"
#include "loop_fixture.hpp"

namespace {

using namespace ai_test;
using namespace ants;
using namespace ants::ai;
using namespace b41;

constexpr int kWallX = 30;                      // the line of rocks across the field
constexpr int kGapY = 30;                       // ... and the gap in it

// A line of rocks across the whole field (hill 0 of the field lies west of it, hills 1 and 3 east), with a gap at kGapY if `gap`
void wall_across(sim::SimulationEngine& sim, bool gap) {
    for (int y = 0; y < 60; ++y) {
        if (gap && y == kGapY) continue;
        sim.set_terrain(kWallX, y, sim::TERRAIN_OBSTACLE);
    }
}

// The world of the plug: a line of rocks with a gap, and a power-up of `kind` in the gap
void plugged_field(sim::SimulationEngine& sim, uint8_t kind = 4, uint32_t seed = 11) {
    empty_field(sim, seed);
    wall_across(sim, true);
    sim.grid_mut().place_powerup(kWallX, kGapY, kind);
}

LevelPlan plan_of(Level level, bool plugs) {
    LevelPlan p = peace_plan(level);
    p.plugs = plugs;
    p.refusal_memory = false;
    return p;
}

// What the seat's ants walk to: whether an ant on `from` has a walk to the queue row of the hill of seat 0 as the map is now
bool walks_home(const sim::SimulationEngine& sim, TileCoord from) {
    const MapInfo map(sim);
    const MapInfo::NowField field = map.field_now(sim.grid(), 0);
    return map.reaches_hill(field, from);
}

}  // namespace

void run_unstick_tests() {
    TEST_CASE("AI57.1 The Engine's Loop: A Thief That Has Raided And Has No Walk Home Stands On The Raid Tile With Its Loot, Saying Can't Go Again And Again, And The Loop Never Ends By Itself") {
        sim::SimulationEngine sim;
        empty_field(sim, 21);
        wall_across(sim, false);                                                              // no walk from the east to the hill of team 0
        const MapInfo map(sim);
        const HillInfo& h1 = map.hill(1);
        sim.set_player_score(1, 300);
        const uint32_t thief = sim.spawn_unit(0, sim::AntType::Thief, TileCoord{h1.raid.x + 2, h1.raid.y});
        sim.apply_command(command_of(CommandType::GroupSpecial, 0, {thief}, h1.entrance.x, h1.entrance.y));
        CantGoTally tally;
        uint32_t clip_ticks = 0;
        for (int t = 0; t < 1500; ++t) {
            sim.tick();
            tally.scan(sim, sim.poll_news_events());
            sim.clear_audio_events();
            const sim::AntUnit& u = sim.get_unit(thief);
            if (t >= 200 && u.state == sim::UnitState::CantGo) ++clip_ticks;
            if (t >= 200) {
                ASSERT_TRUE(u.is_alive() && u.pixel_x / 32 == h1.raid.x && u.pixel_y / 32 == h1.raid.y);   // it never leaves the raid tile ...
                ASSERT_TRUE(u.is_holding() && u.carried_points == 50);                                       // ... nor puts the loot down
            }
        }
        ASSERT_EQ(sim.get_player_score(1), 250);                                              // (the victim lost the points, the thief's team never got them)
        ASSERT_EQ(sim.get_player_score(0), 0);
        ASSERT_TRUE(clip_ticks > 1100);                                                       // it is in the clip nearly all the time
        ASSERT_TRUE(tally.seat(0).reactions > 50);                                            // and says it again and again: every clip of 18 ticks is a reaction
    } TEST_END();

    TEST_CASE("AI57.2 A Power-Up That Plugs The Only Gap Of A Line Of Rocks Is Taken By The Stranded Ant, Which Opens The Way Home; With plan.plugs Off It Stays Plugged") {
        struct Out {
            bool plugged{true};
            size_t combat{0};
            bool walk{false};
            uint32_t sent{0};
            uint32_t opened{0};
        };
        const auto play = [&](bool plugs) {
            sim::SimulationEngine sim;
            plugged_field(sim);
            const uint32_t stranded = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{40, 30});
            (void)stranded;
            Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(plan_of(Level::Medium, plugs)), 4, 4);
            rig.run(900);
            Out out;
            out.plugged = sim.grid().has_powerup_at(TileCoord{kWallX, kGapY});
            out.combat = count_type(sim, 0, sim::AntType::Combat);
            out.walk = walks_home(sim, TileCoord{40, 30});
            out.sent = rig.as<StandardBot>().plugs().sent();
            out.opened = rig.as<StandardBot>().plugs().opened();
            return out;
        };
        const Out on = play(true);
        const Out off = play(false);
        ASSERT_FALSE(on.plugged);                                                             // the ant took the power-up ...
        ASSERT_EQ(on.combat, 1u);                                                             // ... and is the Combat Ant that it makes
        ASSERT_TRUE(on.walk);                                                                 // the way home is open
        ASSERT_EQ(on.sent, 1u);
        ASSERT_EQ(on.opened, 1u);
        ASSERT_TRUE(off.plugged);                                                             // with the rule off nothing is done
        ASSERT_FALSE(off.walk);
        ASSERT_EQ(off.sent, 0u);
        // the stranded ant goes first, though a worker of the hill's part stands nearer the gap
        sim::SimulationEngine sim;
        plugged_field(sim);
        const uint32_t far_ant = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{44, 30});
        const uint32_t near_ant = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{27, 30});
        Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(plan_of(Level::Medium, true)), 4, 4);
        rig.run(900);
        uint32_t who = 0;
        for (const auto& e : rig.sent) {
            if (who == 0 && e.second.type == CommandType::GroupMove && e.second.ants.size() == 1 && e.second.tile_x == kWallX && e.second.tile_y == kGapY) who = e.second.ants[0];
        }
        ASSERT_EQ(who, far_ant);
        ASSERT_TRUE(sim.get_unit(near_ant).type == sim::AntType::Worker);
    } TEST_END();

    TEST_CASE("AI57.3 A Plug That Opens Nothing For The Seat Is Left Where It Is") {
        sim::SimulationEngine sim;
        plugged_field(sim);
        sim.spawn_unit(0, sim::AntType::Worker, TileCoord{12, 30});                           // an ant on the hill's side only; nothing, no ant and no pile, behind the plug
        Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(plan_of(Level::Medium, true)), 4, 4);
        rig.run(900);
        ASSERT_TRUE(sim.grid().has_powerup_at(TileCoord{kWallX, kGapY}));
        ASSERT_EQ(rig.as<StandardBot>().plugs().sent(), 0u);
        ASSERT_EQ(rig.as<StandardBot>().plugs().found(), 0u);
        // a plug between two parts of which neither is the hill's: a second line of rocks far to the east, its gap plugged, an ant in the strip behind it
        sim::SimulationEngine far;
        plugged_field(far);
        for (int y = 0; y < 60; ++y) {
            if (y != kGapY) far.set_terrain(45, y, sim::TERRAIN_OBSTACLE);
        }
        far.grid_mut().place_powerup(45, kGapY, 4);
        far.spawn_unit(0, sim::AntType::Worker, TileCoord{52, 40});
        Rig rig2(far, 0, Level::Medium, std::make_unique<StandardBot>(plan_of(Level::Medium, true)), 4, 4);
        rig2.run(900);
        ASSERT_TRUE(far.grid().has_powerup_at(TileCoord{45, kGapY}));
        ASSERT_TRUE(far.grid().has_powerup_at(TileCoord{kWallX, kGapY}));
        ASSERT_EQ(rig2.as<StandardBot>().plugs().sent(), 0u);
    } TEST_END();

    TEST_CASE("AI57.4 A Pile Behind A Plug That No Ant Stands Behind Sends The Nearest Ant Of The Hill's Part; A Typed Ant Behind The Plug Is Not Used") {
        const auto play = [&](sim::AntType behind) {
            sim::SimulationEngine sim;
            plugged_field(sim);
            add_pile(sim, 42, 30, 12, 50);
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{14, 30});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{14, 34});
            if (behind != sim::AntType::Worker) sim.spawn_unit(0, behind, TileCoord{44, 24});
            Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(plan_of(Level::Medium, true)), 4, 4);
            rig.run(900);
            return std::make_pair(sim.grid().has_powerup_at(TileCoord{kWallX, kGapY}), rig.as<StandardBot>().plugs().sent());
        };
        const auto from_home = play(sim::AntType::Worker);
        ASSERT_FALSE(from_home.first);                                                        // a worker of the hill's part walked to the gap and took it
        ASSERT_EQ(from_home.second, 1u);
        const auto typed = play(sim::AntType::Thief);                                         // a Thief behind it stands there (it would lose its type) but a worker of the hill's part goes all the same
        ASSERT_FALSE(typed.first);
        ASSERT_EQ(typed.second, 1u);
        sim::SimulationEngine only;
        plugged_field(only);
        add_pile(only, 42, 30, 12, 50);
        const uint32_t thief = only.spawn_unit(0, sim::AntType::Thief, TileCoord{44, 24});
        Rig rig(only, 0, Level::Medium, std::make_unique<StandardBot>(plan_of(Level::Medium, true)), 4, 4);
        rig.run(900);
        ASSERT_TRUE(only.grid().has_powerup_at(TileCoord{kWallX, kGapY}));                    // no worker of the hill's part, and the Thief is nobody's to send
        ASSERT_EQ(rig.as<StandardBot>().plugs().sent(), 0u);
        ASSERT_TRUE(only.get_unit(thief).type == sim::AntType::Thief);
    } TEST_END();

    TEST_CASE("AI57.5 The Plug Task And The Trip Task Do Not Send Two Ants To One Power-Up, Whichever Of Them Was First") {
        // a wall near the hill of team 0 with a gap that a Bomber power-up of the own side plugs (the Hard plan wants a Bomber for the own side's power-up: the trip task has its own reason to go there), an ant
        // stranded behind the wall (the plug task's reason), and a worker of the hill's part far from the gap
        const auto play = [&](bool plug_first) {
            sim::SimulationEngine sim;
            empty_field(sim, 12);
            for (int y = 0; y < 60; ++y) {
                if (y != 10) sim.set_terrain(14, y, sim::TERRAIN_OBSTACLE);
            }
            if (plug_first) sim.grid_mut().place_powerup(14, 10, 1);
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{22, 10});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{6, 30});
            LevelPlan plan = peace_plan(Level::Hard);
            plan.plugs = true;
            plan.refusal_memory = false;
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            if (!plug_first) {
                PlugTask::Params quick = rig.as<StandardBot>().plugs().params();
                quick.first_look = 0;                                                          // (the plug task looks at every tick, so that it is the one that sees the power-up that appears at tick 100)
                quick.look_ticks = 1;
                rig.as<StandardBot>().plugs_for_labs().set_params(quick);
            }
            for (int t = 0; t < 700; ++t) {
                if (!plug_first && t == 100) sim.grid_mut().place_powerup(14, 10, 1);              // (the power-up appears while the other task is looking: a dropped one)
                rig.tick();
            }
            uint32_t clicks = 0;
            for (const auto& e : rig.sent) {
                if (e.second.type == CommandType::GroupMove && e.second.ants.size() == 1 && e.second.tile_x == 14 && e.second.tile_y == 10) ++clicks;
            }
            return std::make_pair(clicks, count_type(sim, 0, sim::AntType::Bomber));
        };
        const auto first = play(true);                                                        // the power-up is there at the start: the trip task goes first (the plug task looks from tick 60), the plug task finds an ant on its way
        ASSERT_EQ(first.first, 1u);
        ASSERT_EQ(first.second, 1u);
        const auto later = play(false);                                                       // it appears at tick 100: the plug task goes first, the trip task finds an ant on its way
        ASSERT_EQ(later.first, 1u);
        ASSERT_EQ(later.second, 1u);
    } TEST_END();

    TEST_CASE("AI57.6 The Refusal Memory: A Click The Engine Refuses Twice Is Blocked, The First Refusal Blocks Nothing, And A Carrier Is Never Judged") {
        sim::SimulationEngine sim;
        empty_field(sim, 33);
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx != 0 || dy != 0) sim.set_terrain(40 + dx, 30 + dy, sim::TERRAIN_OBSTACLE);                // a pocket at (40,30): a walk into it finds no path
            }
        }
        const uint32_t ant = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{20, 20});
        const MapInfo map(sim);
        RefusalMemory memory;
        const auto give = [&]() {
            const sim::Command c = command_of(CommandType::GroupMove, 0, {ant}, 40, 30);
            sim.apply_command(c);
            memory.sent(c, sim.current_tick());
        };
        const auto look = [&](uint64_t n) {
            for (uint64_t i = 0; i < n; ++i) {
                tick_all(sim, 1);
                memory.look(BotView::build(sim, 0, &map));
            }
        };
        look(2);
        give();
        look(60);
        ASSERT_EQ(memory.refusals(), 1u);                                                     // the ant stands where it stood: refused
        ASSERT_FALSE(memory.is_blocked(ant, CommandType::GroupMove, TileCoord{40, 30}, sim.current_tick()));      // the first refusal blocks nothing
        give();
        look(60);
        ASSERT_EQ(memory.refusals(), 2u);
        ASSERT_TRUE(memory.is_blocked(ant, CommandType::GroupMove, TileCoord{40, 30}, sim.current_tick()));       // the second does
        ASSERT_FALSE(memory.is_blocked(ant, CommandType::GroupMove, TileCoord{40, 31}, sim.current_tick()));      // only that click
        ASSERT_FALSE(memory.is_blocked(ant, CommandType::GroupAttack, TileCoord{40, 30}, sim.current_tick()));    // only that kind of order
        Orders orders;
        orders.move({ant}, TileCoord{40, 30});
        orders.move({ant}, TileCoord{25, 25});
        const BotView view = BotView::build(sim, 0, &map);
        const std::vector<sim::Command> taken = memory.filter(view, orders);
        ASSERT_EQ(taken.size(), 1u);                                                          // the blocked click goes, the other stays
        ASSERT_EQ(orders.intents().size(), 1u);
        ASSERT_EQ(orders.intents()[0].command.tile_x, 25);
        // the block grows with the third refusal and ends after its time
        look(memory.params().first_block_ticks + 10);
        ASSERT_FALSE(memory.is_blocked(ant, CommandType::GroupMove, TileCoord{40, 30}, sim.current_tick()));
        give();
        look(60);
        ASSERT_TRUE(memory.is_blocked(ant, CommandType::GroupMove, TileCoord{40, 30}, sim.current_tick() + memory.params().first_block_ticks + 5));   // longer than the first block
        // an ant that walks obeyed, and a carrier is not watched
        const uint32_t walker = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{20, 24});
        look(2);                                                                              // (the memory sees where the new ant stands)
        const sim::Command w = command_of(CommandType::GroupMove, 0, {walker}, 23, 24);
        sim.apply_command(w);
        memory.sent(w, sim.current_tick());
        look(150);                                                                            // (it arrives within the 40 ticks of the wait and stands there: it moved, so it obeyed)
        ASSERT_EQ(memory.refusals(), 3u);                                                     // (the walker was not refused)
        const uint32_t carrier = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{20, 28});
        sim.get_unit(carrier).pick_up_food(1, 25);
        const uint32_t into_hill = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{20, 34});
        look(2);                                                                              // (the memory sees where the new ants stand)
        for (int round = 0; round < 2; ++round) {
            const sim::Command a = command_of(CommandType::GroupMove, 0, {carrier}, 40, 30);
            sim.apply_command(a);
            memory.sent(a, sim.current_tick());
            const MapInfo again(sim);
            const HillInfo& h1 = again.hill(1);
            const sim::Command b = command_of(CommandType::GroupMove, 0, {into_hill}, h1.entrance.x, h1.entrance.y);   // a click into another team's mound: the walk is not one that ends in a refusal to read
            sim.apply_command(b);
            memory.sent(b, sim.current_tick());
            look(60);
        }
        ASSERT_EQ(memory.refusals(), 3u);                                                     // a carrier and a click into a mound are never judged
    } TEST_END();

    TEST_CASE("AI57.7 The Stuck Tally Counts A Thief In The Can't Go Loop And A Carrier With No Order Once The Span Is Ten Seconds Old, An Ant At Work Never, And play_match Reports It") {
        sim::SimulationEngine sim;
        empty_field(sim, 44);
        wall_across(sim, false);
        const MapInfo map(sim);
        const HillInfo& h1 = map.hill(1);
        sim.set_player_score(1, 300);
        const uint32_t thief = sim.spawn_unit(0, sim::AntType::Thief, TileCoord{h1.raid.x + 2, h1.raid.y});
        const uint32_t carrier = sim.spawn_unit(2, sim::AntType::Worker, TileCoord{20, 40});
        sim.get_unit(carrier).pick_up_food(1, 25);                                            // (a bite in the hands, no order, no walk: the engine's walk home that ended at once)
        const uint32_t planter = sim.spawn_unit(3, sim::AntType::Bomber, TileCoord{44, 44});
        const uint32_t queuer = sim.spawn_unit(3, sim::AntType::Worker, TileCoord{47, 48});         // a carrier idle at its own hill, where the carriers wait for the gate
        sim.get_unit(queuer).pick_up_food(1, 25);
        sim.apply_command(command_of(CommandType::GroupSpecial, 0, {thief}, h1.entrance.x, h1.entrance.y));
        sim.apply_command(command_of(CommandType::GroupSpecial, 3, {planter}, 46, 46));
        StuckTally tally;
        bool early = false;
        for (int t = 0; t < 1400; ++t) {
            tick_all(sim, 1);
            tally.scan(sim);
            if (t == 150 && tally.seat(0).ants != 0) early = true;
        }
        ASSERT_FALSE(early);                                                                  // (the raid itself is a clip of its own work, and the loop begins after it)
        ASSERT_EQ(tally.seat(0).ants, 1u);                                                    // the thief in the loop: one span ...
        ASSERT_EQ(tally.seat(0).looping, 1u);
        ASSERT_TRUE(tally.seat(0).ticks > 400);                                               // ... that goes on
        ASSERT_EQ(tally.seat(2).ants, 1u);                                                    // the carrier with no order: one span
        ASSERT_EQ(tally.seat(2).carriers, 1u);
        ASSERT_EQ(tally.seat(3).ants, 0u);                                                    // the bomber at work, and the carrier that waits at its own hill: none
        ASSERT_TRUE(tally.episodes().size() >= 2);
    } TEST_END();

    TEST_CASE("AI57.8 A Thief That Raided And Was Walled In On The Raid Tile Is Freed By An Own Fire Ant, Which Puts A Wall Of The Hole Out; With The Rule Off The Thief Stays") {
        struct Out {
            bool free{false};
            size_t walls_left{3};
            uint32_t thief_ticks_in_loop{0};
        };
        const auto play = [&](bool rescue, bool trapped) {
            sim::SimulationEngine sim;
            empty_field(sim, 55);
            const MapInfo probe(sim);
            const HillInfo& h1 = probe.hill(1);
            const std::array<TileCoord, 3> walls = east_tiles(h1);
            const uint32_t thief = sim.spawn_unit(0, sim::AntType::Thief, trapped ? h1.raid : TileCoord{30, 40});
            sim.spawn_unit(0, sim::AntType::Fire, TileCoord{30, 10});
            for (const TileCoord& w : walls) sim.set_fire_at(w, 3600);
            sim.apply_command(command_of(CommandType::GroupMove, 0, {thief}, 20, 20));            // (the thief tries to leave: the clip, and the engine's loop)
            LevelPlan plan = peace_plan(Level::Hard);
            plan.rescue_thief = rescue;
            plan.plugs = false;
            plan.refusal_memory = false;
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            Out out;
            for (int t = 0; t < 700; ++t) {
                rig.tick();
                if (sim.get_unit(thief).state == sim::UnitState::CantGo) ++out.thief_ticks_in_loop;
            }
            const sim::AntUnit& u = sim.get_unit(thief);
            out.free = u.is_alive() && !(u.pixel_x / 32 == h1.raid.x && u.pixel_y / 32 == h1.raid.y);
            out.walls_left = walls_east(sim, h1.origin);
            return out;
        };
        const Out on = play(true, true);
        const Out off = play(false, true);
        ASSERT_TRUE(off.thief_ticks_in_loop > 600);                                           // (the premise: shut in, the thief says it again and again)
        ASSERT_FALSE(off.free);
        ASSERT_EQ(off.walls_left, 3u);
        ASSERT_TRUE(on.free);                                                                 // the Fire Ant put a wall out and the engine's own walk home took the thief out
        ASSERT_TRUE(on.walls_left <= 2u);
        ASSERT_TRUE(on.thief_ticks_in_loop < off.thief_ticks_in_loop);
        const Out other = play(true, false);                                                  // walls of a hole that shuts in nobody are not the Fire Ant's business
        ASSERT_EQ(other.walls_left, 3u);
        // walls in front of another hole that shuts in nobody are left alone though the Fire Ant stands beside them and the shut-in Thief's walls are far away
        {
            sim::SimulationEngine sim;
            empty_field(sim, 57);
            const MapInfo probe(sim);
            const HillInfo& h1 = probe.hill(1);
            const HillInfo& h3 = probe.hill(3);
            const uint32_t thief = sim.spawn_unit(0, sim::AntType::Thief, h1.raid);
            sim.spawn_unit(0, sim::AntType::Fire, TileCoord{h3.origin.x + 8, h3.origin.y + 2});
            for (const TileCoord& w : east_tiles(h1)) sim.set_fire_at(w, 3600);
            for (const TileCoord& w : east_tiles(h3)) sim.set_fire_at(w, 3600);
            sim.apply_command(command_of(CommandType::GroupMove, 0, {thief}, 20, 20));
            LevelPlan plan = peace_plan(Level::Hard);
            plan.plugs = false;
            plan.refusal_memory = false;
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(900);
            ASSERT_EQ(walls_east(sim, h3.origin), 3u);                                        // the walls of the other hole stand
            ASSERT_TRUE(walls_east(sim, h1.origin) <= 2u);                                    // the walls of the Thief's hole do not
        }
        // no Fire Ant yet, a Fire power-up on the own side: a worker fetches it, and then puts the wall out
        sim::SimulationEngine sim;
        empty_field(sim, 56);
        const MapInfo probe(sim);
        const HillInfo& h1 = probe.hill(1);
        const uint32_t thief = sim.spawn_unit(0, sim::AntType::Thief, h1.raid);
        sim.grid_mut().place_powerup(16, 12, 2);
        for (const int dx : {0, 1, 2}) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{8 + dx, 10});
        for (const TileCoord& w : east_tiles(h1)) sim.set_fire_at(w, 3600);
        sim.apply_command(command_of(CommandType::GroupMove, 0, {thief}, 20, 20));
        LevelPlan plan = peace_plan(Level::Hard);
        plan.plugs = false;
        plan.refusal_memory = false;
        plan.secure_side = false;                                                              // (nothing else wants the Fire power-up of the own side: the walled-in Thief is the reason)
        plan.flower_fire = false;
        Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
        rig.run(1800);
        ASSERT_TRUE(count_type(sim, 0, sim::AntType::Fire) >= 1u);                            // the Fire power-up was taken for it
        ASSERT_TRUE(walls_east(sim, h1.origin) <= 2u);                                        // and a wall is out
        ASSERT_TRUE(sim.get_unit(thief).is_alive() && !(sim.get_unit(thief).pixel_x / 32 == h1.raid.x && sim.get_unit(thief).pixel_y / 32 == h1.raid.y));
    } TEST_END();

    TEST_CASE("AI57.9 Whole Matches Of The Map Of The Power-Ups That Plug The Ways: The Bots Strand Fewer Ants And Refuse Fewer Orders Than The Bots Without The Rules, And The Match Replays") {
        assets::LevelData level;
        ASSERT_TRUE(level.load_from_file(std::string(ANTS_SOURCE_DIR) + "/Community-Maps/!SpY VS sPy vs SpY VS sPY! .lvl"));
        const auto play = [&](bool rules, uint32_t seed) {
            ArenaSpec spec;
            spec.level = &level;
            spec.seed = seed;
            spec.max_ticks = 6000;
            spec.record = true;
            for (uint8_t s = 0; s < 4; ++s) {
                BotSpec b;
                b.seat = s;
                b.level = s % 2 == 0 ? Level::Hard : Level::Medium;
                spec.bots.push_back(b);
            }
            spec.factory = [rules](const BotSpec& b) {
                return std::unique_ptr<Bot>(std::make_unique<StandardBot>(b.level, Style::Random, [rules](LevelPlan& plan) {
                    plan.plugs = rules;
                    plan.refusal_memory = rules;
                    plan.rescue_thief = rules;
                    plan.free_thief = rules;
                }));
            };
            return std::make_pair(play_match(spec), spec);
        };
        uint64_t looping_on = 0, looping_off = 0, refused_on = 0, refused_off = 0, orders_on = 0, orders_off = 0, steals_on = 0;
        for (uint32_t seed = 1; seed <= 3; ++seed) {
            const auto on = play(true, seed);
            const auto off = play(false, seed);
            ASSERT_TRUE(on.first.error.empty() && off.first.error.empty());
            for (const ArenaSeatResult& s : on.first.seats) {
                looping_on += s.stuck_looping;
                refused_on += s.refused_orders;
                orders_on += s.orders;
                steals_on += s.steals;
            }
            for (const ArenaSeatResult& s : off.first.seats) {
                looping_off += s.stuck_looping;
                refused_off += s.refused_orders;
                orders_off += s.orders;
            }
            if (seed == 1) {
                const ReplayResult replay = replay_commands(on.second, on.first);
                ASSERT_TRUE(replay.ok);                                                       // the match is its commands: no bot is needed to play it again
            }
        }
        ASSERT_TRUE(steals_on > 0);                                                           // (the arena counts the steals completed: loot carried into the hill)
        ASSERT_TRUE(looping_off > 0);                                                         // (the premise: the bots without the rules strand thieves on this map)
        ASSERT_TRUE(looping_on < looping_off);
        ASSERT_TRUE(refused_on * orders_off < refused_off * orders_on);                       // the share of refused orders falls
    } TEST_END();

    TEST_CASE("AI57.10 The Boundary Click: A Thief In The Engine's Can't-Go Loop Is Clicked Off The Raid Tile At The Tick The Engine Takes It, And Sent Home When A Walk Opens; Under The Rules Before 14, With The Rule Off, Or At Easy It Stays In The Loop") {
        ASSERT_TRUE(plan_for(Level::Hard).free_thief && plan_for(Level::Medium).free_thief && !plan_for(Level::Easy).free_thief);
        {
            LevelPlan p = plan_for(Level::Hard);
            without_unstick_batch(p);                                                         // (the bot as it was: the batch's switch is one of them)
            ASSERT_FALSE(p.free_thief);
        }
        struct Out {
            bool ok{false};
            bool free{false};
            uint32_t loop_ticks{0};
            uint32_t released{0};
            uint32_t released_shut{0};                                                        // the commands the bot gave while the way home was shut
            int32_t score{0};
            uint32_t proposed{0};
            uint32_t freed{0};
            uint32_t homeward{0};
            uint32_t homeward_shut{0};                                                        // the clicks onto the entrance while no walk home existed
            TileCoord target{-1, -1};                                                         // the tile of the first click that left
        };
        const auto play = [&](bool after_commands, bool rule, bool open_later, bool shut_home = true, int open_at = -1) -> Out {
            LoopWorld w(after_commands, 11, shut_home);
            w.run_to_raid_end();
            LevelPlan plan = peace_plan(Level::Hard);
            plan.plugs = false;
            plan.refusal_memory = false;
            plan.rescue_thief = false;
            plan.free_thief = rule;
            RecordingSink sink(w.sim, true);
            BotController c(w.sim, 3);
            c.set_start_hold(0);
            BotSpec spec;
            spec.seat = 0;
            spec.kind = "standard";
            spec.level = Level::Hard;
            auto bot = std::make_unique<StandardBot>(plan);
            const StandardBot* raw = bot.get();
            std::string why;
            Out out;
            if (!c.add(spec, std::move(bot), sink, why)) return out;
            out.ok = true;
            for (int t = 0; t < 500; ++t) {
                if (t == open_at) w.open_rocks();                                             // (a refusal that ends by itself: the wall burns out)
                w.sim.tick();
                c.on_tick(w.sim);
                w.sim.clear_news_events();
                w.sim.clear_audio_events();
                if (w.unit().state == sim::UnitState::CantGo) ++out.loop_ticks;
            }
            out.free = !w.on_raid_tile();
            out.homeward_shut = raw->free_thief().homeward();
            out.released_shut = c.stats(0).released;
            out.score = w.sim.get_player_score(0);
            if (!sink.log.empty()) out.target = TileCoord{sink.log[0].second.tile_x, sink.log[0].second.tile_y};
            if (open_later) {                                                                 // the rings go (the walls burn out, the enemy's wall is down): a walk home exists
                w.open_rocks();
                for (int t = 0; t < 900; ++t) {
                    w.sim.tick();
                    c.on_tick(w.sim);
                    w.sim.clear_news_events();
                    w.sim.clear_audio_events();
                }
                out.score = w.sim.get_player_score(0);
            }
            out.released = c.stats(0).released;
            out.proposed = raw->free_thief().proposed();
            out.freed = raw->free_thief().freed();
            out.homeward = raw->free_thief().homeward();
            return out;
        };
        const Out on = play(true, true, true);
        ASSERT_TRUE(on.ok);
        ASSERT_TRUE(on.free);                                                                 // the thief left the raid tile ...
        ASSERT_TRUE(on.loop_ticks < 120);                                                     // ... after a few cycles of the loop (the rule waits for two looks, and the controller for the tick)
        ASSERT_TRUE(on.proposed >= 1 && on.freed >= 1);
        ASSERT_EQ(on.homeward_shut, 0u);                                                      // (no click onto the entrance while no walk exists: it would be refused)
        ASSERT_TRUE(on.target.chebyshev_dist(kLoopHome0) <= 16);                              // the tile of the pocket that is nearest to the own hill ...
        ASSERT_FALSE(on.target.x >= 39 && on.target.x <= 44 && on.target.y >= 39 && on.target.y <= 44);   // ... and not one of the hill's tiles, where the loop would go on
        ASSERT_TRUE(on.homeward >= 1);                                                        // when the walk home exists it is clicked onto the entrance
        ASSERT_TRUE(on.score >= 50);                                                          // and the loot is banked: the steal is complete (the Thief raids again, the hole being open: 50 points a raid)
        ASSERT_EQ(on.released_shut, 1u);                                                      // (one click paid for while the way was shut: the boundary click; the way home is the second)
        const Out open = play(true, true, false, false);                                      // a Thief whose way home is open is never clicked away: the engine's walk takes it home and the loot is banked
        ASSERT_EQ(open.proposed, 0u);
        ASSERT_TRUE(open.loop_ticks == 0);
        ASSERT_TRUE(open.score >= 50 && open.score % 50 == 0);                                // (a Thief whose way home is open banks the loot at once, and raids again)
        const Out transient = play(true, true, false, true, 25);                              // the wall burns out after 25 ticks: the thief walks home by itself, and no click is proposed for a refusal that was not a loop
        ASSERT_TRUE(transient.ok);
        ASSERT_EQ(transient.proposed, 0u);
        ASSERT_TRUE(transient.score >= 50);
        const Out before = play(false, true, false);                                          // the rules before 14: no tick takes the click
        ASSERT_FALSE(before.free);
        ASSERT_TRUE(before.loop_ticks > 400);
        ASSERT_EQ(before.released, 0u);                                                       // (and the click that never has its tick costs nothing)
        const Out off = play(true, false, false);                                             // the rule off: the bot as it was
        ASSERT_TRUE(open.ok && before.ok && off.ok);
        ASSERT_FALSE(off.free);
        ASSERT_TRUE(off.loop_ticks > 400);
        ASSERT_EQ(off.proposed, 0u);
        // a Thief with loot that stands idle and was never clicked away is not sent home by the task: it is not the task's
        {
            sim::SimulationEngine sim;
            empty_field(sim, 58);
            const uint32_t thief = sim.spawn_unit(0, sim::AntType::Thief, TileCoord{30, 30});
            sim.get_unit(thief).steal_points(50);
            LevelPlan plan = peace_plan(Level::Hard);
            plan.plugs = false;
            plan.refusal_memory = false;
            plan.rescue_thief = false;
            RecordingSink sink(sim, true);
            BotController c(sim, 3);
            c.set_start_hold(0);
            BotSpec spec;
            spec.seat = 0;
            spec.kind = "standard";
            spec.level = Level::Hard;
            auto bot = std::make_unique<StandardBot>(plan);
            const StandardBot* raw = bot.get();
            std::string why;
            ASSERT_TRUE(c.add(spec, std::move(bot), sink, why));
            for (int t = 0; t < 400; ++t) {
                sim.tick();
                c.on_tick(sim);
                sim.clear_news_events();
                sim.clear_audio_events();
            }
            ASSERT_EQ(raw->free_thief().homeward(), 0u);
            ASSERT_EQ(raw->free_thief().proposed(), 0u);
        }
        // a Thief whose part of the map holds no tile that is not a tile of a hill has no tile to be clicked to: nothing is proposed (the walls are the Fire Ant's business, AI57.8). Two such parts: the raid tile
        // alone (the mound and a ring of rocks round it), and the raid tile with the column of the walls and the row above the mound, which are walkable but are tiles of the hill
        for (const TileCoord centre : {TileCoord{41, 41}, TileCoord{42, 41}}) {
            LoopWorld w(true, 12, false);
            w.run_to_raid_end();
            rocks_ring(w.sim, centre, 3);
            rocks_ring(w.sim, TileCoord{22, 22}, 6);
            LevelPlan plan = peace_plan(Level::Hard);
            plan.plugs = false;
            plan.refusal_memory = false;
            plan.rescue_thief = false;
            RecordingSink sink(w.sim, true);
            BotController c(w.sim, 3);
            c.set_start_hold(0);
            BotSpec spec;
            spec.seat = 0;
            spec.kind = "standard";
            spec.level = Level::Hard;
            auto bot = std::make_unique<StandardBot>(plan);
            const StandardBot* raw = bot.get();
            std::string why;
            ASSERT_TRUE(c.add(spec, std::move(bot), sink, why));
            for (int t = 0; t < 300; ++t) {
                w.sim.tick();
                c.on_tick(w.sim);
                w.sim.clear_news_events();
                w.sim.clear_audio_events();
            }
            ASSERT_EQ(raw->free_thief().proposed(), 0u);
            ASSERT_EQ(c.stats(0).released, 0u);
        }
    } TEST_END();
}
