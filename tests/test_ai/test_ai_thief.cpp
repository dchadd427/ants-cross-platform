// AI53: the answer to a raid (docs/BOT_TACTICS.md, "Thieves: the answer to a raid"): the thief batch of the standard bot in hand-made worlds with the real engine.
//
// The meeting (ThiefGuardTask) and the trap are built and OFF in every plan (docs/BOTS.md says why): the tests of them (AI53.1 to AI53.3) switch them on themselves.
//
//   AI53.1  an enemy Thief that comes for the hole is met: ants stand on the three tiles in front of it, the raid order ends in "Can't go there." and the victim keeps its points (without the
//           guard the raid goes through); the strikers hit the Thief that stands in front of the hole
//   AI53.2  who is no target: a Thief far away or walking off, an ant of the ally, a hole that the walls have shut (a Thief that walks to it or stands in front of it), and nobody is sent when only the
//           last ants are left
//   AI53.3  the trap: a Thief shut in on the raid tile with the loot by the own walls is let out (a wall is put out) when the strikers stand ready; with nobody ready, a Thief without loot or the trap off the walls stay; no blocker walks onto the tiles in front of the hole while the Thief is on the raid tile
//   AI53.4  the raid offence looks again: a thief on its way to a hole that is shut meanwhile (walls, a box that falls, rocks across the way, an ant on the raid tile) goes to the other hill or stops
//           (and never arrives at a hole that it cannot enter); with the switch off it arrives and shows "Can't go there."; the hill is left alone for 300 ticks after walls or a missing walk, not after
//           an ant that stood on the raid tile for a moment
//   AI53.5  a Fire Ant that stands where no walk leads to the hole gets no wall order (a corner row of ISLANDS), and the Fire power-up of such a place is not taken
//   AI53.7  a keeper that cannot walk to a wall (a rock in a lake) is given no order for it, to build, to renew or to put out an enemy wall: it is not asked again for 600 ticks and is the economy's
//           meanwhile; on a walled map the walk is not asked
//   AI53.8  a keeper that stands on the mound of its hill or in the fire of its own wall is given the wall order (the rule of AI53.7 judges where a walk leaves from, not the tile it stands on)
//   AI53.6  the batch is one switch (without_thief_batch, `--tune thief_batch=0`): with it off nothing of it happens; Medium and Hard ship the look again and the Fire Ant rule, Easy none of it, no level the meeting or the trap
#include <algorithm>
#include <array>
#include <cstdint>
#include <tuple>
#include <vector>

#include "ai_test.hpp"
#include "ants_ai/arena.hpp"
#include "ants_ai/standard_bot.hpp"
#include "b41_helpers.hpp"
#include "island_fixture.hpp"

namespace {

using namespace ai_test;
using namespace ants;
using namespace ants::ai;
using namespace b41;

// A raider that is not the bot's: it is ordered at the hill of team 0 at the first tick and again every time it stands idle, shows can't-go or is empty-handed on the way (what a person does)
struct Raider {
    uint32_t id{0};
    uint32_t orders{0};
    void drive(sim::SimulationEngine& sim, uint8_t team, TileCoord click) {
        const sim::AntUnit& u = sim.get_unit(id);
        if (!u.is_alive()) return;
        if (sim.current_tick() % 4 != 0) return;
        if (!u.is_holding() && (u.state == sim::UnitState::Idle || u.state == sim::UnitState::CantGo)) {
            sim.apply_command(command_of(CommandType::GroupSpecial, team, {id}, click.x, click.y));
            ++orders;
        }
    }
};

LevelPlan quiet_plan(Level level) {
    LevelPlan p = peace_plan(level);
    return p;
}

}  // namespace

void run_thief_tests() {
    TEST_CASE("AI53.1 An Enemy Thief That Comes For The Hole Is Met: Ants Stand On The Three Tiles In Front Of It, The Raid Order Ends In Can't Go There And The Victim Keeps Its Points (Without The Guard The Raid Goes Through); The Striker Hits The Thief That Stands In Front Of The Hole")
    {
        sim::SimulationEngine probe;
        empty_field(probe, 71);
        const MapInfo pmap(probe);
        const HillInfo& hill = pmap.hill(0);
        const std::array<TileCoord, 3> east = east_tiles(hill);
        struct Out {
            int32_t score{0};
            uint32_t meets{0};
            uint32_t strikes{0};
            size_t on_east{0};
            uint8_t thief_hp{0};
            uint32_t cantgo{0};
        };
        const auto play = [&](bool guard, uint64_t ticks, size_t workers, uint32_t kill_after = 100) {
            sim::SimulationEngine sim;
            empty_field(sim, 71);
            sim.set_player_score(0, 300);
            for (size_t i = 0; i < workers; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{10 + static_cast<int>(i % 3), 10 + static_cast<int>(i / 3)});
            Raider raider;
            raider.id = sim.spawn_unit(1, sim::AntType::Thief, TileCoord{30, 8});
            sim.apply_command(command_of(CommandType::GroupSpecial, 1, {raider.id}, hill.entrance.x, hill.entrance.y));
            LevelPlan plan = quiet_plan(Level::Hard);
            plan.thief_guard = guard;
            plan.thief_guard_kill_after = kill_after;
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            Out out;
            rig.after_sim_tick = [&]() {
                raider.drive(sim, 1, hill.entrance);
                if (sim.get_unit(raider.id).state == sim::UnitState::CantGo) ++out.cantgo;
            };
            rig.run(ticks);
            out.score = sim.get_player_score(0);
            const StandardBot& bot = rig.as<StandardBot>();
            out.meets = bot.thief_guard().meets_started();
            out.strikes = bot.thief_guard().strikes();
            for (const sim::AntSnapshot& a : sim.get_world_state().ants) {
                if (a.player_id != 0) continue;
                for (const TileCoord& e : east) out.on_east += (a.tile_x == e.x && a.tile_y == e.y) ? 1u : 0u;
            }
            out.thief_hp = static_cast<uint8_t>(sim.get_unit(raider.id).hp);
            return out;
        };
        const Out met = play(true, 300, 6);                                                                    // (the meeting at its strongest: it lasts 480 ticks at most, and its strikers pull the blockers off the tiles when they chase, docs/BOTS.md)
        ASSERT_TRUE(met.meets >= 1u);
        ASSERT_EQ(met.score, 300);                                                                            // nothing was taken
        ASSERT_EQ(met.on_east, 3u);                                                                           // three ants hold the three tiles
        ASSERT_TRUE(met.cantgo > 0u);                                                                         // the Thief stood in front of the hole and showed it
        ASSERT_TRUE(met.strikes >= 1u);                                                                       // and the striker went at it
        ASSERT_TRUE(met.thief_hp < 10u);
        const Out strikers = play(true, 300, 6, 1000000);                                                      // (no kill mode: the blockers never strike, so every blow is a striker's)
        ASSERT_TRUE(strikers.strikes >= 1u);
        ASSERT_TRUE(strikers.thief_hp < 10u);
        const Out open = play(false, 700, 6);                                                                  // (the plan as it ships: no meeting)
        ASSERT_EQ(open.meets, 0u);
        ASSERT_EQ(open.score, 250);                                                                           // the raid goes through
        ASSERT_EQ(open.cantgo, 0u);
    } TEST_END();

    TEST_CASE("AI53.2 No Target: A Thief Far Away, A Thief That Walks Off, A Hole That The Walls Have Shut While The Thief Walks, The Thief Of The Ally; And Nobody Is Sent When Only The Last Ants Are Left")
    {
        sim::SimulationEngine probe;
        empty_field(probe, 72);
        const MapInfo pmap(probe);
        const HillInfo& hill = pmap.hill(0);
        const std::array<TileCoord, 3> east = east_tiles(hill);
        const auto meets = [&](const std::function<void(sim::SimulationEngine&, uint32_t&)>& set_up, size_t workers, uint64_t ticks) {
            sim::SimulationEngine sim;
            empty_field(sim, 72);
            sim.set_player_score(0, 300);
            for (size_t i = 0; i < workers; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{10 + static_cast<int>(i % 3), 10 + static_cast<int>(i / 3)});
            uint32_t thief = 0;
            set_up(sim, thief);
            for (int t = 0; t < 30; ++t) sim.tick();                                                          // (the thief is on its way: what it was ordered shows on the screen)
            LevelPlan plan = quiet_plan(Level::Hard);
            plan.thief_guard = true;
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(ticks);
            return rig.as<StandardBot>().thief_guard().meets_started();
        };
        // (a) the premise: a thief at 18 tiles that comes on is met
        ASSERT_TRUE(meets([&](sim::SimulationEngine& sim, uint32_t& t) {
                        t = sim.spawn_unit(1, sim::AntType::Thief, TileCoord{22, 8});
                        sim.apply_command(command_of(CommandType::GroupSpecial, 1, {t}, hill.entrance.x, hill.entrance.y));
                    }, 6, 60) >= 1u);
        // (b) too far (40 tiles), and one that walks off
        ASSERT_EQ(meets([&](sim::SimulationEngine& sim, uint32_t& t) {
                      t = sim.spawn_unit(1, sim::AntType::Thief, TileCoord{48, 8});
                      sim.apply_command(command_of(CommandType::GroupSpecial, 1, {t}, hill.entrance.x, hill.entrance.y));
                  }, 6, 40), 0u);
        ASSERT_EQ(meets([&](sim::SimulationEngine& sim, uint32_t& t) {
                      t = sim.spawn_unit(1, sim::AntType::Thief, TileCoord{14, 8});
                      sim.apply_command(command_of(CommandType::GroupMove, 1, {t}, 40, 30));
                  }, 6, 60), 0u);
        // (c) the walls shut the hole: a thief that walks in is not met (the walls stop it), and the ants stay at their work
        ASSERT_EQ(meets([&](sim::SimulationEngine& sim, uint32_t& t) {
                      for (const TileCoord& e : east) sim.set_fire_at(e, 3600);
                      t = sim.spawn_unit(1, sim::AntType::Thief, TileCoord{22, 8});
                      sim.apply_command(command_of(CommandType::GroupSpecial, 1, {t}, hill.entrance.x, hill.entrance.y));
                  }, 6, 60), 0u);
        // (c2) a Thief that stands in front of the shut hole is no target either (the walls keep it out), and the same Thief in front of an open hole is one
        const auto stands = [&](bool walls) {
            return meets([&](sim::SimulationEngine& sim, uint32_t& t) {
                if (walls) {
                    for (const TileCoord& e : east) sim.set_fire_at(e, 3600);
                }
                t = sim.spawn_unit(1, sim::AntType::Thief, TileCoord{12, 8});
            }, 6, 60);
        };
        ASSERT_TRUE(stands(false) >= 1u);
        ASSERT_EQ(stands(true), 0u);
        // (d) only the last ants: two workers (the reserve of a fight) are not sent
        ASSERT_EQ(meets([&](sim::SimulationEngine& sim, uint32_t& t) {
                      t = sim.spawn_unit(1, sim::AntType::Thief, TileCoord{22, 8});
                      sim.apply_command(command_of(CommandType::GroupSpecial, 1, {t}, hill.entrance.x, hill.entrance.y));
                  }, 2, 60), 0u);
        // (e) a thief of the ally is no target
        {
            sim::SimulationEngine sim;
            empty_field(sim, 72);
            sim.set_player_score(0, 300);
            for (int i = 0; i < 6; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{10 + i % 3, 10 + i / 3});
            const uint32_t ally_thief = sim.spawn_unit(1, sim::AntType::Thief, TileCoord{22, 8});
            Command invite = command_of(CommandType::AllianceInvite, 0, {}, 0, 0);
            invite.other_player = 1;
            sim.apply_command(invite);
            Command accept = command_of(CommandType::AllianceAccept, 1, {}, 0, 0);
            accept.other_player = 0;
            sim.apply_command(accept);
            sim.apply_command(command_of(CommandType::GroupMove, 1, {ally_thief}, hill.entrance.x + 3, hill.entrance.y + 3));
            LevelPlan plan = quiet_plan(Level::Hard);
            plan.thief_guard = true;
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(60);
            ASSERT_EQ(rig.as<StandardBot>().thief_guard().meets_started(), 0u);
        }
    } TEST_END();

    TEST_CASE("AI53.3 The Trap: A Thief Shut In On The Raid Tile With The Loot By The Own Walls Is Let Out When The Strikers Stand Ready (A Wall Is Put Out); With Nobody Ready, With A Thief That Took Nothing, Or With The Trap Off The Walls Stay")
    {
        sim::SimulationEngine probe;
        empty_field(probe, 73);
        const MapInfo pmap(probe);
        const HillInfo& hill = pmap.hill(0);
        const std::array<TileCoord, 3> east = east_tiles(hill);
        struct Out {
            uint32_t opens{0};
            size_t walls_after{3};
            bool thief_out{false};
            uint8_t thief_hp{10};
        };
        const auto play = [&](size_t workers, bool trap, int32_t score = 300) {
            sim::SimulationEngine sim;
            empty_field(sim, 73);
            sim.set_player_score(0, score);
            for (size_t i = 0; i < workers; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{9 + static_cast<int>(i % 3), 9 + static_cast<int>(i / 3)});
            sim.spawn_unit(0, sim::AntType::Fire, TileCoord{10, 12});
            for (const TileCoord& e : east) sim.set_fire_at(e, 3600);
            const uint32_t thief = sim.spawn_unit(1, sim::AntType::Thief, hill.raid);
            sim.start_thief_infiltration(thief, 0);
            LevelPlan plan = quiet_plan(Level::Hard);
            plan.thief_guard = true;                                                                          // (the trap is a part of the meeting)
            plan.thief_trap = trap;
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            Out out;
            rig.run(900);
            const sim::AntUnit& u = sim.get_unit(thief);
            out.opens = rig.as<StandardBot>().thief_guard().trap_opens();
            out.walls_after = walls_east(sim, hill.origin);
            out.thief_out = u.is_alive() && !(u.pos.x == hill.raid.x && u.pos.y == hill.raid.y);
            out.thief_hp = static_cast<uint8_t>(u.hp);
            return out;
        };
        const Out ready = play(6, true);
        ASSERT_TRUE(ready.opens >= 1u);
        ASSERT_TRUE(ready.thief_out);                                                                         // it came out of the hole
        // (not checked: that one wall stays out and that the Thief dies. Neither happens today: the keeper puts out the walls one after the other until none is left, and the Thief walks off
        // alive with the loot. That is why the trap ships off, docs/BOTS.md; a test of either would pin a rule that the trap does not keep.)
        {
            // (the fighters that were taken are on their way to the posts, not at them: no wall is put out yet)
            sim::SimulationEngine sim;
            empty_field(sim, 73);
            sim.set_player_score(0, 300);
            for (size_t i = 0; i < 6; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{9 + static_cast<int>(i % 3), 9 + static_cast<int>(i / 3)});
            sim.spawn_unit(0, sim::AntType::Fire, TileCoord{10, 12});
            for (const TileCoord& e : east) sim.set_fire_at(e, 3600);
            const uint32_t thief = sim.spawn_unit(1, sim::AntType::Thief, hill.raid);
            sim.start_thief_infiltration(thief, 0);
            LevelPlan plan = quiet_plan(Level::Hard);
            plan.thief_guard = true;
            plan.thief_trap = true;
            plan.thief_trap_wait = 1;                                                                          // (the wait between two openings would hide it: the first opening waits for the fighters alone)
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(100);                                                                                      // (the Thief has the loot from about tick 70; the fighters start to move at tick 80 and are not at the posts yet)
            ASSERT_EQ(rig.as<StandardBot>().thief_guard().trap_opens(), 0u);
        }
        const Out none = play(1, true);                                                                        // (one worker and the Fire Ant: the last ants, nobody is ready)
        ASSERT_EQ(none.opens, 0u);
        ASSERT_EQ(none.walls_after, 3u);                                                                       // the walls stay
        ASSERT_FALSE(none.thief_out);
        // no blocker is taken while the Thief is on the raid tile (it would be shut in with the loot): the meeting holds the strikers only (open hole, six workers near it)
        {
            sim::SimulationEngine sim;
            empty_field(sim, 73);
            sim.set_player_score(0, 300);
            for (int i = 0; i < 6; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{hill.origin.x + 7 + i % 3, hill.origin.y + 1 + i / 3});
            const uint32_t thief = sim.spawn_unit(1, sim::AntType::Thief, hill.raid);
            sim.start_thief_infiltration(thief, 0);
            LevelPlan plan = quiet_plan(Level::Hard);
            plan.thief_guard = true;
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(60);                                                                                       // (the raid clip is not over: the Thief is on the raid tile)
            ASSERT_TRUE(rig.as<StandardBot>().thief_guard().meets_started() >= 1u);                            // (it is waited for)
            ASSERT_EQ(rig.as<StandardBot>().thief_guard().fighters_of(thief).size(), static_cast<size_t>(plan.thief_guard_force));
        }
        const Out off = play(6, false);
        ASSERT_EQ(off.opens, 0u);
        ASSERT_EQ(off.walls_after, 3u);
        const Out empty_handed = play(6, true, 0);                                                              // (the victim had nothing: the Thief takes no loot, nothing is lost by keeping the walls)
        ASSERT_EQ(empty_handed.opens, 0u);
    } TEST_END();

    TEST_CASE("AI53.4 The Raid Offence Looks Again: A Thief On Its Way To A Hole That Is Shut Meanwhile Goes To The Other Hill Or Stops, And Never Arrives At A Hole That It Cannot Enter (With The Switch Off It Arrives And Shows Can't Go There); The Hill Is Left Alone For 300 Ticks After Walls Or A Missing Walk, Not After An Ant That Stands On The Raid Tile For A Moment")
    {
        sim::SimulationEngine probe;
        empty_field(probe, 74);
        const MapInfo pmap(probe);
        const HillInfo& h1 = pmap.hill(1);
        const std::array<TileCoord, 3> east1 = east_tiles(h1);
        // what shuts the hole while the thief walks to it: its owner lights all three walls for good (Walls) or for 120 ticks (Burning); an ant of a third team stands on the raid tile for 70 ticks
        // (Ant); a line of rocks across the whole field cuts the thief off from the hole (Cut)
        enum class Shut { Walls, Burning, Ant, Cut };
        struct Out {
            uint32_t rechecks{0};
            uint32_t retargets{0};
            uint64_t cantgo{0};
            int32_t score1{0};
            int32_t score2{0};
            uint32_t raids{0};
            uint64_t off{0};                                                                                    // the tick of the stop that called the raid off (0: none)
            uint64_t again{0};                                                                                  // the tick of the first raid order after it (0: none)
            int32_t thief_x{0};
        };
        const auto play = [&](bool recheck, bool other_open, Shut how) {
            sim::SimulationEngine sim;
            empty_field(sim, 74);
            sim.set_player_score(1, 300);
            sim.set_player_score(2, other_open ? 250 : 0);
            const uint32_t thief = sim.spawn_unit(0, sim::AntType::Thief, TileCoord{30, 12});
            LevelPlan plan = quiet_plan(Level::Hard);
            plan.raid_recheck = recheck;
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            CantGoTally tally;
            rig.count_with(&tally);
            bool shut = false;
            uint32_t ant = 0;
            uint64_t ant_since = 0;
            for (int t = 0; t < 900; ++t) {
                rig.tick();
                const sim::AntUnit& u = sim.get_unit(thief);
                const int32_t ahead = h1.raid.x - u.pixel_x / 32;                                                // the tiles to the hole
                if (!shut && u.is_alive() && u.state == sim::UnitState::Walking && (how == Shut::Cut ? u.pixel_x / 32 >= 34 : ahead <= 12 && ahead >= 4)) {
                    shut = true;
                    if (how == Shut::Walls || how == Shut::Burning) {
                        for (const TileCoord& e : east1) sim.set_fire_at(e, how == Shut::Walls ? 3600 : 120);
                    } else if (how == Shut::Ant) {
                        ant = sim.spawn_unit(3, sim::AntType::Worker, h1.raid);
                        ant_since = sim.current_tick();
                    } else {
                        for (int y = 0; y < 60; ++y) sim.set_terrain(46, y, sim::TERRAIN_OBSTACLE);
                    }
                }
                if (ant != 0 && sim.current_tick() == ant_since + 70) sim.apply_command(command_of(CommandType::GroupMove, 3, {ant}, 40, 40));       // it walks off the tile
            }
            Out out;
            const StandardBot& bot = rig.as<StandardBot>();
            out.rechecks = bot.raids().rechecks();
            out.retargets = bot.raids().retargets();
            out.cantgo = tally.seat(0).reactions;
            out.score1 = sim.get_player_score(1);
            out.score2 = sim.get_player_score(2);
            out.raids = bot.raids().raids_ordered();
            out.thief_x = sim.get_unit(thief).pixel_x / 32;
            for (const auto& e : rig.proposed) {
                if (e.second.ants.size() != 1 || e.second.ants[0] != thief) continue;
                if (e.second.type == CommandType::Stop && out.off == 0 && shut) out.off = e.first;
                else if (e.second.type == CommandType::GroupSpecial && out.off != 0 && out.again == 0) out.again = e.first;
            }
            return out;
        };
        ASSERT_TRUE(plan_for(Level::Hard).raid_recheck && plan_for(Level::Medium).raid_recheck);
        ASSERT_FALSE(plan_for(Level::Easy).raid_recheck);
        const Out after = play(true, false, Shut::Walls);
        ASSERT_TRUE(after.rechecks >= 1u);                                                                     // the hole was found shut on the way
        ASSERT_EQ(after.cantgo, 0u);                                                                           // and the thief never stood in front of it
        ASSERT_EQ(after.score1, 300);                                                                          // nothing was taken
        const Out before = play(false, false, Shut::Walls);
        ASSERT_EQ(before.rechecks, 0u);
        ASSERT_TRUE(before.cantgo > 0u);                                                                       // it arrived and showed "Can't go there."
        // no walk reaches the hole any more (a line of rocks across the field): the raid is called off at the look of the walk, the thief stops short of the rocks; with the switch off it walks to them
        const Out cut = play(true, false, Shut::Cut);
        ASSERT_TRUE(cut.rechecks >= 1u);
        ASSERT_TRUE(cut.off != 0u);
        ASSERT_TRUE(cut.thief_x < 44);
        const Out cut_off = play(false, false, Shut::Cut);
        ASSERT_EQ(cut_off.rechecks, 0u);
        ASSERT_TRUE(cut_off.thief_x >= 44);
        // what shut the hole decides how long the hill is left alone: walls that burn out (a bomb stays, a missing walk stays) for the 300 ticks of the plan, an ant that stands on the raid tile for
        // a moment not at all (the thief goes again when it has walked off)
        const Out burnt = play(true, false, Shut::Burning);
        ASSERT_TRUE(burnt.rechecks >= 1u);
        ASSERT_TRUE(burnt.off != 0u && burnt.again != 0u);
        ASSERT_TRUE(burnt.again >= burnt.off + 300u);
        const Out passing = play(true, false, Shut::Ant);
        ASSERT_TRUE(passing.rechecks >= 1u);
        ASSERT_TRUE(passing.off != 0u && passing.again != 0u);
        ASSERT_TRUE(passing.again < passing.off + 150u);
        ASSERT_TRUE(passing.score1 < 300);                                                                     // and it raided
        // the box of the victim falls below what a raid is worth while the thief walks (its owner spent it on eggs): the thief stops, and with the switch off it takes what is left
        for (const bool recheck : {true, false}) {
            sim::SimulationEngine sim;
            empty_field(sim, 74);
            sim.set_player_score(1, 300);
            const uint32_t thief = sim.spawn_unit(0, sim::AntType::Thief, TileCoord{30, 12});
            LevelPlan plan = quiet_plan(Level::Hard);
            plan.raid_recheck = recheck;
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            bool dropped = false;
            for (int t = 0; t < 700; ++t) {
                rig.tick();
                const sim::AntUnit& u = sim.get_unit(thief);
                if (!dropped && u.is_alive() && u.state == sim::UnitState::Walking && u.pixel_x / 32 > h1.raid.x - 10 && u.pixel_x / 32 < h1.raid.x - 4) {
                    sim.set_player_score(1, 10);
                    dropped = true;
                }
            }
            ASSERT_TRUE(dropped);
            ASSERT_EQ(rig.as<StandardBot>().raids().rechecks() >= 1u, recheck);
            ASSERT_EQ(sim.get_player_score(1), recheck ? 10 : 0);
        }
        const Out other = play(true, true, Shut::Walls);
        ASSERT_TRUE(other.retargets >= 1u);                                                                    // another hill with points: the thief went there
        ASSERT_EQ(other.score1, 300);
        ASSERT_TRUE(other.score2 < 250);
        ASSERT_EQ(other.cantgo, 0u);
    } TEST_END();

    TEST_CASE("AI53.5 A Fire Ant Is Not Left Where No Walk Leads Home: On ISLANDS Four Hard Bots With The Rule Take No Fire Power-Up Of A Corner Row (No Fire Ant Of Theirs Stands On An Island That Its Hill Does Not Walk To, 1,500 Ticks On Seeds 1 To 4, Sampled Every 20 Ticks), And Count The Trips That They Skipped; The Wall Order Of A Keeper That Cannot Walk To The Hole Is Dropped")
    {
        const auto stranded = [&](bool rule) {
            uint32_t total = 0;
            uint32_t skips = 0;
            uint32_t unreachable = 0;
            for (uint32_t seed = 1; seed <= 4; ++seed) {
                island_test::Match m;
                m.init("ISLANDS", seed, 0x0F, Level::Hard, 0, [rule](LevelPlan& p) { p.fire_home_only = rule; });
                const MapInfo map(m.sim);
                for (int t = 0; t < 75; ++t) {                                                                 // (every 20 ticks of 1,500: the Fire Ants that stand on another island at that moment, each counted once for every look)
                    m.run(20);
                    for (const sim::AntSnapshot& a : m.sim.get_world_state().ants) {
                        if (a.player_id > 3 || a.raw_type != sim::AntType::Fire || a.hp == 0) continue;
                        if (map.ant_component(a.player_id, TileCoord{a.tile_x, a.tile_y}) != map.hill_component(a.player_id)) ++total;
                    }
                }
                for (uint8_t t = 0; t < 4; ++t) {
                    skips += m.bot(t)->powerups().stranded_skips();
                    unreachable += m.bot(t)->walls().unreachable();
                }
            }
            return std::array<uint32_t, 3>{total, skips, unreachable};
        };
        const auto with = stranded(true);
        const auto without = stranded(false);
        ASSERT_EQ(with[0], 0u);                                                                               // no Fire Ant on another island
        ASSERT_TRUE(with[1] > 0u);                                                                            // the trips that the rule skipped
        ASSERT_EQ(without[1], 0u);
        ASSERT_EQ(without[2], 0u);
    } TEST_END();

    TEST_CASE("AI53.7 A Keeper That Cannot Walk To A Wall Is Given No Order For It, Whatever The Wall Is For: A Fire Ant On A Rock In A Lake (No Walk Leads To The Wall) Gets No Order To Build, To Renew Or To Put Out An Enemy Wall, Is Not Asked Again For 600 Ticks And Is The Economy's In The Meantime; With The Rule Off The Order Is Sent And Refused; A Walled Map Is Judged By Its Goals, Not By This Rule")
    {
        // what the keeper is asked for: a wall in front of the hole (Build: none is up, a Thief is in sight), a wall that is due to be put out and lit again (Renew: the three walls are up since the start
        // and are due 3,300 ticks in), an enemy wall near the hill (Counter: no Thief is in sight)
        enum class Work { Build, Renew, Counter };
        struct Out {
            uint32_t unreachable{0};
            uint32_t walls_ordered{0};
            size_t specials{0};                                                                                 // the orders of the keeper's own: a special onto a tile
            bool kept{false};                                                                                   // the keeper was still the wall task's after a look that dropped an order
            bool gate_map{false};
        };
        const auto play = [&](bool rule, Work work, bool gate) {
            WallWorld w;
            w.build(work != Work::Counter, 0);
            for (int dy = -2; dy <= 2; ++dy) {
                for (int dx = -2; dx <= 2; ++dx) {
                    if (dx != 0 || dy != 0) w.sim.set_terrain(30 + dx, 30 + dy, sim::TERRAIN_WATER);
                }
            }
            if (gate) {                                                                                         // a pile behind a wall of fire across the field: the map is walled, the bot plays it by its goals
                place_crackers(w.sim, 12, 40, 25);
                for (int x = 0; x < 60; ++x) w.sim.set_fire_at(TileCoord{x, 20}, 1000000);
            }
            const MapInfo wmap(w.sim);
            const HillInfo& hill0 = wmap.hill(0);
            if (work == Work::Renew) {
                for (const TileCoord& e : east_tiles(hill0)) w.sim.set_fire_at(e, 3600);
            } else if (work == Work::Counter) {
                for (int i = 0; i < 3; ++i) w.sim.set_fire_at(TileCoord{hill0.origin.x + 6, hill0.origin.y + 2 + i}, 3500);
            }
            const uint32_t keeper = w.sim.spawn_unit(0, sim::AntType::Fire, TileCoord{30, 30});
            LevelPlan plan = peace_plan(Level::Hard);
            plan.secure_side = false;
            plan.reach_guard = false;                                                                           // (the raw orders: the guard of the orders would take a refused one out)
            plan.fire_home_only = rule;
            Rig rig(w.sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(3);
            w.let_enemy_play();
            Out out;
            uint32_t seen = 0;
            const uint64_t ticks = work == Work::Renew ? 3560 : 500;
            for (uint64_t t = 0; t < ticks; ++t) {
                rig.tick();
                const StandardBot& bot = rig.as<StandardBot>();
                if (bot.walls().unreachable() != seen) {
                    seen = bot.walls().unreachable();
                    out.kept = out.kept || bot.ledger().owner(keeper) == StandardBot::kWalls;
                }
            }
            const StandardBot& bot = rig.as<StandardBot>();
            out.unreachable = bot.walls().unreachable();
            out.walls_ordered = bot.walls().walls_ordered();
            out.gate_map = bot.gate_map();
            for (const auto& e : rig.proposed) out.specials += (e.second.type == CommandType::GroupSpecial && e.second.ants.size() == 1 && e.second.ants[0] == keeper) ? 1u : 0u;
            return out;
        };
        for (const Work work : {Work::Build, Work::Renew, Work::Counter}) {
            const Out on = play(true, work, false);
            ASSERT_EQ(on.specials, 0u);
            ASSERT_TRUE(on.unreachable >= 1u);
            ASSERT_TRUE(on.unreachable <= 3u);                                                                    // (one look for each of the three walls, then 600 ticks of peace)
            ASSERT_FALSE(on.gate_map);
            if (work != Work::Counter) ASSERT_FALSE(on.kept);                                                    // (a wall to build or to renew: the ant is the economy's again at once)
            const Out off = play(false, work, false);
            ASSERT_TRUE(off.specials >= 1u);                                                                      // (the order that ends in Can't go there.)
            ASSERT_EQ(off.unreachable, 0u);
        }
        // a walled map: the keeper is judged by the goals of the map, the walk is not asked
        const Out walled = play(true, Work::Build, true);
        ASSERT_TRUE(walled.gate_map);
        ASSERT_EQ(walled.unreachable, 0u);
        ASSERT_TRUE(walled.specials >= 1u);
    } TEST_END();

    TEST_CASE("AI53.8 A Keeper That Stands On The Mound Of Its Hill Or In The Fire Of Its Own Wall Is Given The Wall Order: The Tile It Stands On Is Not A Tile That A Walk Enters, And The Walk Leaves From It (With The Rule On, As With It Off)")
    {
        WallWorld pw;
        pw.build(true, 0);
        const MapInfo pmap(pw.sim);
        const HillInfo& hill = pmap.hill(0);
        const auto play = [&](TileCoord at, bool fire, bool rule, uint32_t& unreachable, size_t& specials) {
            WallWorld w;
            w.build(true, 0);
            const uint32_t keeper = w.sim.spawn_unit(0, sim::AntType::Fire, at);
            if (fire) w.sim.set_fire_at(at, 3600);
            LevelPlan plan = peace_plan(Level::Hard);
            plan.secure_side = false;
            plan.reach_guard = false;
            plan.fire_home_only = rule;
            Rig rig(w.sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(3);
            w.let_enemy_play();
            rig.run(500);
            unreachable = rig.as<StandardBot>().walls().unreachable();
            specials = 0;
            for (const auto& e : rig.proposed) specials += (e.second.type == CommandType::GroupSpecial && e.second.ants.size() == 1 && e.second.ants[0] == keeper) ? 1u : 0u;
        };
        for (const bool fire : {false, true}) {
            const TileCoord at = fire ? TileCoord{14, 20} : TileCoord{hill.origin.x + 1, hill.origin.y + 1};
            uint32_t unreachable = 0;
            size_t specials = 0;
            play(at, fire, false, unreachable, specials);
            ASSERT_TRUE(specials >= 1u);                                                                          // (the premise: a keeper there is sent to its walls)
            play(at, fire, true, unreachable, specials);
            ASSERT_TRUE(specials >= 1u);
            ASSERT_EQ(unreachable, 0u);
        }
    } TEST_END();

    TEST_CASE("AI53.6 The Batch Is One Switch: without_thief_batch Is The Bot As It Was (No Meeting, No Look Again, No Trap, No Stranded Skip), Easy Never Had It, Medium And Hard Have The Look Again And The Fire Ant Rule On, No Level Has The Meeting Or The Trap")
    {
        for (const Level level : {Level::Medium, Level::Hard}) {
            const LevelPlan shipped = plan_for(level);
            ASSERT_TRUE(shipped.raid_recheck && shipped.fire_home_only);
            ASSERT_FALSE(shipped.thief_guard || shipped.thief_trap);                                          // (built and off: docs/BOTS.md)
            LevelPlan off = shipped;
            off.thief_guard = off.thief_trap = true;                                                          // (every flag on, as the experiments set them)
            without_thief_batch(off);
            ASSERT_FALSE(off.raid_recheck || off.thief_guard || off.thief_trap || off.fire_home_only);
        }
        const LevelPlan easy = plan_for(Level::Easy);
        ASSERT_FALSE(easy.raid_recheck || easy.thief_guard || easy.thief_trap || easy.fire_home_only);
        // a match with the batch off plays the same as one with every flag of it off by hand, and the guard that is off plays the same whatever its other keys say: the same commands, tick for tick, in
        // a world where the guard that is on acts (a Thief comes for the hole of a hill with six workers)
        sim::SimulationEngine probe;
        empty_field(probe, 75);
        const TileCoord entrance = MapInfo(probe).hill(0).entrance;
        using Orders = std::vector<std::tuple<uint64_t, int, std::vector<uint32_t>, int, int>>;
        const auto play = [&](const LevelPlan& plan) {
            sim::SimulationEngine sim;
            empty_field(sim, 75);
            sim.set_player_score(0, 300);
            place_crackers(sim, 16, 14, 25);                                                                    // (the economy has work: the commands are not none)
            for (int i = 0; i < 6; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{10 + i % 3, 10 + i / 3});
            const uint32_t t = sim.spawn_unit(1, sim::AntType::Thief, TileCoord{30, 8});
            sim.apply_command(command_of(CommandType::GroupSpecial, 1, {t}, entrance.x, entrance.y));
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(400);
            Orders out;
            for (const auto& e : rig.proposed) out.emplace_back(e.first, static_cast<int>(e.second.type), e.second.ants, static_cast<int>(e.second.tile_x), static_cast<int>(e.second.tile_y));
            return std::make_pair(out, rig.as<StandardBot>().thief_guard().meets_started());
        };
        LevelPlan pa = plan_for(Level::Hard);
        pa.thief_guard = pa.thief_trap = true;                                                                  // (the meeting is on until the switch turns it off)
        without_thief_batch(pa);
        LevelPlan pb = plan_for(Level::Hard);
        pb.raid_recheck = pb.thief_guard = pb.thief_trap = pb.fire_home_only = false;
        LevelPlan pc = pb;
        pc.thief_guard_force = 5;
        pc.thief_guard_block = 0;
        pc.thief_guard_radius = 40;
        pc.thief_trap_wait = 7;
        LevelPlan pd = pb;
        pd.thief_guard = true;
        const auto ra = play(pa);
        const auto rb = play(pb);
        const auto rc = play(pc);
        const auto rd = play(pd);
        ASSERT_TRUE(!rb.first.empty());
        ASSERT_TRUE(rd.second >= 1u);                                                                           // (the premise: with the meeting on, the bot meets this Thief)
        ASSERT_TRUE(rd.first != rb.first);                                                                      // ... and plays another game
        ASSERT_TRUE(ra.first == rb.first);
        ASSERT_TRUE(rc.first == rb.first);
        ASSERT_EQ(ra.second, 0u);
        ASSERT_EQ(rb.second, 0u);
        ASSERT_EQ(rc.second, 0u);
    } TEST_END();
}
