// AI56: when the food is gone (docs/BOT_TACTICS.md, "When the food is gone"): the food-gone batch of the standard bot and the idle tally of the arena, in hand-made worlds with the real engine.
//
// With no food that the hill can walk to (every pile eaten, or what is left out of reach and the island, gate and field work banking nothing), a bot does not stand: after LevelPlan::gone_wait ticks
// its free ants steal for a smaller loot, fight, and heal. The cases play a bot of a level through the rig (b41_helpers.hpp) on the empty field of the older tests (no pile at all, so the food is gone
// from the first look) and count what it sends; every one has the same world with the batch off (`without_gone_batch`, `--tune gone=0`), which is the bot as it was.
//
//   AI56.1  the batch begins gone_wait ticks after the work ended, at every level, and not before; with a pile that the hill walks to, or with the batch off, it never does
//   AI56.2  a Thief steals for the level's smaller loot (Hard 10, Medium 15, Easy 30) when the food is gone, and not for less; the same Thief with the batch off waits for the level's usual loot
//   AI56.3  the last ants of a bot (two free ants make an assault) fight only a team that has more to lose than the bot (or that is well ahead of it), and never at Easy; a bot with ants to spare fights at once; the batch off: nobody
//   AI56.4  an idle Swimmer, Bomber or Fire Ant is not sent into a fight with the food gone (a worker is; a Thief stays free for the raids)
//   AI56.5  an idle ant that is hurt walks home to heal from wherever a walk leads, with the batch on; a healthy one stays; with the batch off none goes
//   AI56.6  the arena's teams (ArenaSpec::teams) are made before the first tick: the chosen pair and the other two are allies, free for all makes none, a seat that does not play is refused
//   AI56.8  an idle Bomber or Swimmer takes a Fire power-up within a trip after standing idle for idle_swap_ticks, while the bot has no Fire Ant (Medium and Hard, not Easy; not with the switch off)
//   AI56.9  with the food gone an idle Swimmer (gone_swap) becomes the Thief; a Bomber or Fire Ant does not for the steals when an enemy box shows enough and the hill walks to that hill: never when it does not
//   AI56.10 the game type 187 has no food by its rules: the batch never begins there
//   AI56.11 with the food gone a fight is taken only when it is won: an assault asks for gone_odds_percent of the strength near the target, and gone_wipe_odds_percent against a team that a wipe-out forces to hatch
//   AI56.12 the stall detector's no-point rule counts the food that the hill walks to: a lunchbox that a ring of rocks shuts off is none of it (the breach of a walled map is not cut off by a dead ant's lunchbox
//           in the field), one in the open is; the switch off is the rule as it was
//   AI56.7  the arena's idle tally counts the ants that stand idle from five seconds on (nothing but standing bots: all of them), the ants and the idle ones from the last food taken to the end, and the
//           raids that Thieves begin; a match whose food is taken has a last food and fewer ants after it than in all
#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "ai_test.hpp"
#include "ants_ai/arena.hpp"
#include "ants_ai/standard_bot.hpp"
#include "b41_helpers.hpp"

namespace {

using namespace ai_test;
using namespace ants;
using namespace ants::ai;
using namespace b41;

LevelPlan plan_of(Level level, bool gone) {
    LevelPlan p = plan_for(level);
    if (!gone) without_gone_batch(p);
    return p;
}

// Whether the bot proposed (at any look) an order of `type` that names `ant`
bool proposed_for(const Rig& rig, CommandType type, uint32_t ant) {
    for (const auto& e : rig.proposed) {
        if (e.second.type == type && std::find(e.second.ants.begin(), e.second.ants.end(), ant) != e.second.ants.end()) return true;
    }
    return false;
}

// Whether the bot proposed (at any look) a move of `ant` onto the tile
bool moved_onto(const Rig& rig, uint32_t ant, TileCoord tile) {
    for (const auto& e : rig.proposed) {
        if (e.second.type == CommandType::GroupMove && e.second.tile_x == tile.x && e.second.tile_y == tile.y && std::find(e.second.ants.begin(), e.second.ants.end(), ant) != e.second.ants.end()) return true;
    }
    return false;
}

constexpr TileCoord kEntrance{5, 5};                     // the entrance of the hill of seat 0 (its hill is at (4, 4))

}  // namespace

void run_gone_tests() {
    TEST_CASE("AI56.1 The Batch Begins gone_wait Ticks After The Work Ended, At Every Level, And Not Before; With A Pile That The Hill Walks To, Or With The Batch Off, It Never Does")
    {
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            const LevelPlan plan = plan_for(level);
            ASSERT_TRUE(plan.gone);
            for (int variant = 0; variant < 3; ++variant) {                                  // 0: no food, 1: a pile that the hill walks to, 2: no food and the batch off
                sim::SimulationEngine sim;
                empty_field(sim, 561);
                for (int i = 0; i < 3; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{20 + i, 20});
                sim.spawn_unit(1, sim::AntType::Worker, TileCoord{44, 44});
                if (variant == 1) add_pile(sim, 24, 24, 400, 25);
                Rig rig(sim, 0, level, std::make_unique<StandardBot>(plan_of(level, variant != 2)), 4, 4);
                uint64_t first = 0;
                for (uint64_t t = 0; t < plan.gone_wait + 600; ++t) {
                    rig.tick();
                    if (first == 0 && rig.as<StandardBot>().tactics().gone) first = t + 1;
                }
                if (variant == 0) {
                    ASSERT_TRUE(first >= plan.gone_wait);                                    // not before the wait
                    ASSERT_TRUE(first <= plan.gone_wait + 200);                              // and soon after it
                } else {
                    ASSERT_EQ(first, 0u);
                }
            }
        }
    } TEST_END();

    TEST_CASE("AI56.2 A Thief Steals For The Level's Smaller Loot When The Food Is Gone (Hard 10, Medium 15, Easy 30) And Not For Less; With The Batch Off It Waits For The Level's Usual Loot")
    {
        struct Row {
            Level level;
            int32_t loot;                                                                    // the smallest box that the batch raids
        };
        for (const Row row : {Row{Level::Hard, 10}, Row{Level::Medium, 15}, Row{Level::Easy, 30}}) {
            for (const int32_t box : {row.loot - 5, row.loot}) {
                for (const bool gone : {true, false}) {
                    sim::SimulationEngine sim;
                    empty_field(sim, 562);
                    sim.set_player_score(1, box);
                    const uint32_t thief = sim.spawn_unit(0, sim::AntType::Thief, TileCoord{20, 20});
                    sim.spawn_unit(0, sim::AntType::Worker, TileCoord{22, 20});
                    sim.spawn_unit(0, sim::AntType::Worker, TileCoord{22, 21});
                    const LevelPlan plan = plan_of(row.level, gone);
                    Rig rig(sim, 0, row.level, std::make_unique<StandardBot>(plan), 4, 4);
                    rig.run(plan_for(row.level).gone_wait + 900);
                    const bool raided = rig.as<StandardBot>().raids().raids_ordered() >= 1 && proposed_for(rig, CommandType::GroupSpecial, thief);
                    ASSERT_EQ(raided, gone && box >= row.loot);                              // (the usual loot of every level is above these boxes: 15, 30 and none)
                }
            }
        }
    } TEST_END();

    TEST_CASE("AI56.3 The Last Ant Of A Bot Fights Only A Team That Has More To Lose Than The Bot Or That Is Well Ahead Of It, And Never At Easy; A Bot With Ants To Spare Fights At Once; The Batch Off: Nobody Does")
    {
        struct Case {
            Level level;
            int32_t mine;                                                                    // the bot's box
            int32_t theirs;                                                                  // the enemy's box
            size_t ants;                                                                     // the bot's ants (one or two are its last ants)
            bool expect;                                                                     // the batch sends an ant at the enemy
        };
        const std::vector<Case> cases = {
            {Level::Hard, 0, 300, 2, true},            // a wipe-out costs the enemy 200 and the bot nothing
            {Level::Hard, 400, 100, 2, false},         // the leader risks nothing for a smaller team
            {Level::Hard, 300, 300, 2, false},         // equal: a gamble with nothing to win
            {Level::Hard, 100, 500, 2, true},          // well behind: a team that is behind loses no place by a lost fight
            {Level::Hard, 0, 300, 1, false},           // one ant is no assault: it takes two free ants, as in the level's own assault
            {Level::Medium, 0, 300, 2, true},
            {Level::Medium, 400, 100, 2, false},
            {Level::Easy, 0, 300, 2, false},           // Easy keeps its last ants out of every fight (an assault of two free ants would be allowed)
            {Level::Hard, 400, 100, 4, true},          // ants to spare: the assault begins at once
        };
        for (const Case& c : cases) {
            for (const bool gone : {true, false}) {
                sim::SimulationEngine sim;
                empty_field(sim, 563);
                sim.set_player_score(0, c.mine);
                sim.set_player_score(1, c.theirs);
                sim.spawn_unit(0, sim::AntType::Worker, TileCoord{30, 30});
                for (size_t i = 1; i < c.ants; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{30 + static_cast<int32_t>(i), 31});
                const uint32_t enemy = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{33, 30});
                LevelPlan plan = plan_of(c.level, gone);
                plan.idle_hunt = false;                                                      // (with idle_hunt and no food only one ant is the last: the reserve of the older rule is two)
                Rig rig(sim, 0, c.level, std::make_unique<StandardBot>(plan), 4, 4);
                rig.run(plan_for(c.level).gone_wait + 700);
                bool attacked = false;                                                       // (the target is thrown by a blow, so the tile of the order is not asked: any attack is the assault)
                for (const auto& e : rig.proposed) attacked = attacked || e.second.type == CommandType::GroupAttack;
                static_cast<void>(enemy);
                // (no blow is struck, so nothing but an assault sends an attack; without the batch the last ant of a bot never does, and the older assault of the roomy cases waits for its own time and surplus)
                if (!gone) ASSERT_FALSE(attacked && c.ants <= 2);
                else ASSERT_EQ(attacked, c.expect);
            }
        }
    } TEST_END();

    TEST_CASE("AI56.4 An Idle Swimmer, Bomber Or Fire Ant Is Not Sent Into A Fight With The Food Gone (Measured: It Costs Ants And Forced Hatches), A Worker Is, And A Thief Stays Free For The Raids")
    {
        sim::SimulationEngine sim;
        empty_field(sim, 564);
        const uint32_t bomber = sim.spawn_unit(0, sim::AntType::Bomber, TileCoord{30, 30});
        const uint32_t swimmer = sim.spawn_unit(0, sim::AntType::Swimmer, TileCoord{31, 30});
        const uint32_t fire = sim.spawn_unit(0, sim::AntType::Fire, TileCoord{32, 30});
        const uint32_t thief = sim.spawn_unit(0, sim::AntType::Thief, TileCoord{33, 30});
        const uint32_t worker = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{30, 31});
        const uint32_t worker2 = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{31, 31});
        sim.spawn_unit(1, sim::AntType::Worker, TileCoord{36, 30});
        const LevelPlan plan = plan_of(Level::Hard, true);
        Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
        rig.run(plan.gone_wait + 700);
        ASSERT_TRUE(proposed_for(rig, CommandType::GroupAttack, worker) || proposed_for(rig, CommandType::GroupAttack, worker2));
        for (const uint32_t typed : {bomber, swimmer, fire, thief}) ASSERT_FALSE(proposed_for(rig, CommandType::GroupAttack, typed));
    } TEST_END();

    TEST_CASE("AI56.5 An Idle Ant That Is Hurt Walks Home To Heal From Wherever A Walk Leads When The Food Is Gone; A Healthy One Stays; With The Batch Off None Goes")
    {
        for (const unsigned hp : {5u, 9u}) {
            for (const bool gone : {true, false}) {
                sim::SimulationEngine sim;
                empty_field(sim, 565);
                const uint32_t ant = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{40, 40});
                sim.get_unit(ant).hp = static_cast<uint8_t>(hp);
                sim.spawn_unit(0, sim::AntType::Worker, TileCoord{41, 40});
                sim.spawn_unit(0, sim::AntType::Worker, TileCoord{42, 40});
                const LevelPlan plan = plan_of(Level::Hard, gone);
                Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
                rig.run(plan_for(Level::Hard).gone_wait + 300);
                bool sent = false;
                for (const auto& e : rig.proposed) {
                    sent = sent || (e.second.type == CommandType::GroupMove && e.second.tile_x == kEntrance.x && e.second.tile_y == kEntrance.y &&
                                    std::find(e.second.ants.begin(), e.second.ants.end(), ant) != e.second.ants.end());
                }
                ASSERT_EQ(sent, gone && hp == 5u);
                if (gone && hp == 5u) {
                    rig.run(1500);
                    ASSERT_EQ(snapshot_of(sim, ant) != nullptr ? snapshot_of(sim, ant)->hp : 0u, 10u);                  // it entered the hill and has all its hit points
                }
            }
        }
    } TEST_END();

    TEST_CASE("AI56.6 The Arena's Teams Are Made Before The First Tick: The Chosen Pair And The Other Two Are Allies, Free For All Makes None, And A Seat That Does Not Play Is Refused")
    {
        const auto play = [&](sim::StartTeams teams, uint8_t roster, std::array<uint8_t, 4>& ally) {
            ArenaSpec spec;
            spec.level = &level_of("TINY");
            spec.seed = 3;
            spec.max_ticks = 40;
            spec.teams = teams;
            for (uint8_t seat = 0; seat < 4; ++seat) {
                if (((roster >> seat) & 1u) == 0) continue;
                BotSpec b;
                b.seat = seat;
                b.kind = "idle";
                b.level = Level::Medium;
                spec.bots.push_back(b);
            }
            spec.inspect = [&](const sim::SimulationEngine& e) {
                for (uint8_t s = 0; s < 4; ++s) ally[s] = e.get_world_state().player_alliances[s];
            };
            return play_match(spec);
        };
        std::array<uint8_t, 4> ally{};
        ASSERT_TRUE(play(sim::StartTeams{true, 0, 2}, 0x0F, ally).error.empty());
        ASSERT_TRUE(ally[0] == 2 && ally[2] == 0 && ally[1] == 3 && ally[3] == 1);
        ASSERT_TRUE(play(sim::StartTeams{}, 0x0F, ally).error.empty());
        for (const uint8_t a : ally) ASSERT_TRUE(a >= sim::MAX_PLAYERS);
        ASSERT_TRUE(play(sim::StartTeams{true, 0, 1}, 0x07, ally).error.empty());            // three seats: the pair, the third alone
        ASSERT_TRUE(ally[0] == 1 && ally[1] == 0 && ally[2] >= sim::MAX_PLAYERS);
        ASSERT_FALSE(play(sim::StartTeams{true, 0, 3}, 0x07, ally).error.empty());          // seat 3 does not play
    } TEST_END();

    TEST_CASE("AI56.7 The Arena's Idle Tally Counts The Ants That Stand Idle From Five Seconds On, The Ants And The Idle Ones From The Last Food Taken To The End, And The Raids That Thieves Begin")
    {
        {   // four bots that stand still: every ant is idle from the sixth second, and no food is ever taken
            ArenaSpec spec;
            spec.level = &level_of("TINY");
            spec.seed = 5;
            spec.max_ticks = 1200;
            for (uint8_t seat = 0; seat < 4; ++seat) {
                BotSpec b;
                b.seat = seat;
                b.kind = "idle";
                b.level = Level::Medium;
                spec.bots.push_back(b);
            }
            const ArenaResult r = play_match(spec);
            ASSERT_TRUE(r.error.empty());
            ASSERT_EQ(r.last_food, 0u);
            for (const ArenaSeatResult& s : r.seats) {
                ASSERT_TRUE(s.ant_ticks > 0);
                ASSERT_EQ(s.after_ant_ticks, s.ant_ticks);                                   // no food was ever taken: all of it is "after"
                ASSERT_EQ(s.after_idle_ticks, s.idle_ticks);
                ASSERT_TRUE(s.idle_ticks * 100 >= s.ant_ticks * 80);                         // all but the first five seconds of a minute ...
                ASSERT_TRUE(s.idle_ticks < s.ant_ticks);                                     // ... and not those
                ASSERT_EQ(s.raids, 0u);
            }
        }
        {   // workers eat: the food is taken, the last bite is at a tick of the match, and what follows is shorter than the match
            ArenaSpec spec;
            spec.level = &level_of("TINY");
            spec.seed = 5;
            for (uint8_t seat = 0; seat < 2; ++seat) {
                BotSpec b;
                b.seat = seat;
                b.kind = "worker";
                b.level = Level::Hard;
                spec.bots.push_back(b);
            }
            const ArenaResult r = play_match(spec);
            ASSERT_TRUE(r.error.empty());
            ASSERT_TRUE(r.last_food > 0 && r.last_food < r.ticks);
            for (const ArenaSeatResult& s : r.seats) {
                ASSERT_TRUE(s.after_ant_ticks < s.ant_ticks);
                ASSERT_TRUE(s.idle_ticks >= s.after_idle_ticks);
            }
        }
        {   // a Thief that raids is counted with the points it takes (the engine names the team a Thief raids only while it raids)
            sim::SimulationEngine sim;
            empty_field(sim, 566);
            sim.set_player_score(1, 120);
            const uint32_t thief = sim.spawn_unit(0, sim::AntType::Thief, TileCoord{44, 10});
            const MapInfo map(sim);
            const TileCoord click = map.hill(1).entrance;
            sim.apply_command(command_of(CommandType::GroupSpecial, 0, {thief}, click.x, click.y));
            IdleTally tally;
            for (uint64_t t = 0; t < 600; ++t) {
                sim.tick();
                sim.clear_news_events();
                sim.clear_audio_events();
                tally.scan(sim);
            }
            ASSERT_EQ(tally.seat(0).raids, 1u);
            ASSERT_TRUE(tally.seat(0).raid_points > 0 && tally.seat(0).raid_points <= 50);
            ASSERT_EQ(tally.seat(1).raids, 0u);
        }
    } TEST_END();

    TEST_CASE("AI56.8 An Idle Bomber Or Swimmer Takes A Fire Power-Up Within A Trip After Standing Idle For idle_swap_ticks While The Bot Has No Fire Ant (Medium And Hard, Not Easy, Not With The Switch Off)")
    {
        struct Case {
            Level level;
            sim::AntType kind;                                                               // what the idle ant is
            bool fire_ant;                                                                   // the bot has a Fire Ant already
            bool on;                                                                         // the plan's switch
            bool expect;                                                                     // the ant takes the power-up
        };
        const std::vector<Case> cases = {
            {Level::Hard, sim::AntType::Bomber, false, true, true},
            {Level::Hard, sim::AntType::Swimmer, false, true, true},
            {Level::Medium, sim::AntType::Bomber, false, true, true},
            {Level::Easy, sim::AntType::Bomber, false, true, false},       // Easy's plan has no swap
            {Level::Hard, sim::AntType::Bomber, true, true, false},        // a Fire Ant is there: the power-up is not wanted
            {Level::Hard, sim::AntType::Bomber, false, false, false},      // the switch is off
            {Level::Hard, sim::AntType::Combat, false, true, false},       // the fighter keeps its type
        };
        for (const Case& c : cases) {
            sim::SimulationEngine sim;
            empty_field(sim, 568);
            const uint32_t ant = sim.spawn_unit(0, c.kind, TileCoord{10, 10});
            if (c.fire_ant) sim.spawn_unit(0, sim::AntType::Fire, TileCoord{9, 9});
            sim.grid_mut().place_powerup(14, 10, static_cast<uint8_t>(sim::AntType::Fire));
            LevelPlan plan = plan_of(c.level, false);                                        // (a world with no food: the food-gone batch is off, the swap is the subject)
            plan.idle_swap = plan_for(c.level).idle_swap && c.on;
            if (c.level == Level::Hard) ASSERT_TRUE(plan_for(c.level).idle_swap && plan_for(c.level).idle_swap_ticks < plan_for(Level::Medium).idle_swap_ticks);
            Rig rig(sim, 0, c.level, std::make_unique<StandardBot>(plan), 4, 4);
            const uint32_t wait = plan_for(c.level).idle_swap_ticks;
            rig.run(wait > 40 ? wait - 40 : 1);
            ASSERT_FALSE(moved_onto(rig, ant, TileCoord{14, 10}));                           // (not before it has stood idle long enough)
            rig.run(wait + 600);
            const bool took = sim.get_unit(ant).type == sim::AntType::Fire;
            ASSERT_EQ(took, c.expect);
            ASSERT_EQ(moved_onto(rig, ant, TileCoord{14, 10}), c.expect);
            if (c.expect) ASSERT_TRUE(rig.as<StandardBot>().powerups().swaps_started() >= 1u);
        }
    } TEST_END();

    TEST_CASE("AI56.9 With The Food Gone An Idle Swimmer Becomes The Thief For The Steals When An Enemy Box Shows Enough And The Hill Walks To That Hill; Never When It Does Not, Never Before The Batch Begins, And Never A Bomber Or A Fire Ant")
    {
        for (const sim::AntType kind : {sim::AntType::Swimmer, sim::AntType::Bomber, sim::AntType::Fire}) {
            for (const bool wall : {false, true}) {                                          // a wall of rock between the hills: no walk leads to the enemy hill
                for (const bool gone : {true, false}) {
                    sim::SimulationEngine sim;
                    empty_field(sim, 569);
                    if (wall) {
                        for (int32_t y = 0; y < 60; ++y) {
                            for (int32_t x = 20; x < 24; ++x) sim.grid_mut().set_terrain(x, y, sim::TERRAIN_OBSTACLE);
                        }
                    }
                    sim.set_player_score(1, 150);
                    const uint32_t ant = sim.spawn_unit(0, kind, TileCoord{12, 30});
                    const uint32_t enemy = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{48, 30});
                    sim.apply_command(command_of(CommandType::GroupMove, 1, {enemy}, 49, 30));                // an enemy that plays: the plan wants a Thief from the start, the batch or not
                    sim.grid_mut().place_powerup(16, 30, static_cast<uint8_t>(sim::AntType::Thief));
                    const LevelPlan plan = plan_of(Level::Hard, gone);
                    Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
                    rig.run(plan_for(Level::Hard).gone_wait - 40);
                    ASSERT_FALSE(moved_onto(rig, ant, TileCoord{16, 30}));                           // (the batch has not begun: the ant is not sent)
                    rig.run(900);
                    const bool thief = sim.get_unit(ant).type == sim::AntType::Thief;
                    ASSERT_EQ(thief, kind == sim::AntType::Swimmer && gone && !wall);
                }
            }
        }
    } TEST_END();

    TEST_CASE("AI56.10 The Game Type 187 Has No Food By Its Rules: The Batch Never Begins There, With The Brawl On Or Off")
    {
        for (const bool brawl : {true, false}) {
            sim::SimulationEngine sim;
            sim.set_game_mode(sim::GameMode::Kills187);
            empty_field(sim, 5610);
            for (int i = 0; i < 3; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{30 + i, 30});
            sim.spawn_unit(1, sim::AntType::Worker, TileCoord{40, 30});
            LevelPlan plan = plan_for(Level::Hard);
            plan.brawl187 = plan.brawl187 && brawl;
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            bool began = false;
            for (uint64_t t = 0; t < plan.gone_wait + 600; ++t) {
                rig.tick();
                began = began || rig.as<StandardBot>().tactics().gone;
            }
            ASSERT_FALSE(began);
        }
    } TEST_END();

    TEST_CASE("AI56.11 With The Food Gone A Fight Is Taken Only When It Is Won: An Assault Asks For gone_odds_percent Of The Strength Near The Target, And gone_wipe_odds_percent Against A Team That A Wipe-Out Forces To Hatch")
    {
        struct Case {
            size_t enemies;                                                                  // the enemy's ants (Hard: a team of two or fewer with a box of 60 or more is a wipe-out target)
            int32_t box;                                                                     // the enemy's box
            uint32_t odds;                                                                   // gone_odds_percent
            uint32_t wipe_odds;                                                              // gone_wipe_odds_percent
            bool expect;                                                                     // the bot attacks
        };
        const std::vector<Case> cases = {
            {3, 0, 1, 100000, true},                   // three ants, no box: the ordinary odds decide ...
            {3, 0, 100000, 1, false},                  // ... and not the wipe-out's
            {3, 300, 100000, 1, false},                // three ants are no wipe-out
            {2, 300, 100000, 1, true},                 // two ants with a box: the wipe-out's odds decide ...
            {2, 300, 1, 100000, false},                // ... and not the ordinary ones
            {2, 0, 100000, 1, false},                  // two ants and no box: nothing to force
        };
        for (const Case& c : cases) {
            sim::SimulationEngine sim;
            empty_field(sim, 5611);
            sim.set_player_score(1, c.box);
            for (int i = 0; i < 5; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{30, 28 + i});
            for (size_t i = 0; i < c.enemies; ++i) sim.spawn_unit(1, sim::AntType::Worker, TileCoord{36, 29 + static_cast<int32_t>(i)});
            LevelPlan plan = plan_of(Level::Hard, true);
            plan.behind_war = plan.catchup = false;                                          // (a box of 300 is a lead to catch up with: the losers' rules are not the subject)
            plan.gone_odds_percent = c.odds;
            plan.gone_wipe_odds_percent = c.wipe_odds;
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
            rig.run(plan_for(Level::Hard).gone_wait + 700);
            bool attacked = false;
            for (const auto& e : rig.proposed) attacked = attacked || e.second.type == CommandType::GroupAttack;
            ASSERT_EQ(attacked, c.expect);
        }
    } TEST_END();

    TEST_CASE("AI56.12 The Stall Detector's No-Point Rule Counts The Food That The Hill Walks To: A Lunchbox That A Ring Of Rocks Shuts Off Is None Of It, One In The Open Is, And With The Switch Off Any Lunchbox Was")
    {
        for (const bool ring : {true, false}) {
            for (const bool rule : {true, false}) {
                sim::SimulationEngine sim;
                empty_field(sim, 5612);
                for (int i = 0; i < 3; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{10 + i, 10});
                if (ring) {                                                                  // a closed room in the field (the analysis of the start knows it: no walk of the hill leads in)
                    for (int d = -3; d <= 3; ++d) {
                        for (const TileCoord& r : {TileCoord{30 + d, 27}, TileCoord{30 + d, 33}, TileCoord{27, 30 + d}, TileCoord{33, 30 + d}}) sim.set_terrain(r.x, r.y, sim::TERRAIN_OBSTACLE);
                    }
                }
                LevelPlan plan = plan_of(Level::Hard, false);                                // (no food-gone batch: the stall detector is the subject)
                plan.repeat_limit = 0;                                                       // (the clock rule alone)
                plan.stall_ticks = 400;
                plan.fallback_ticks = 1000;
                plan.stall_walkable = rule;
                Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 4);
                const uint32_t x = ring ? 30u : 14u;                                              // a dead ant's lunchbox, dropped after the start: the analysis has no pile for it
                sim.grid_mut().drop_lunchbox(x, x, 40);
                rig.run(1600);
                const bool stalled = rig.as<StandardBot>().stalls() > 0;
                ASSERT_EQ(stalled, !ring || !rule);
            }
        }
    } TEST_END();
}
