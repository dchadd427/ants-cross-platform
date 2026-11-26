// AI59: standing on power-ups (docs/BOT_CONTEST.md, "Standing on a power-up"; plan.power_stand, off in every shipped plan; expires with v0.21.8): the original's can't-go cancel, as a bot gives it. In hand-made worlds with the real engine
// and the bot controller (the chain of a stand is the controller's: it needs the sink's clock), and in whole matches.
//
//   AI59.1  the window of the cancel: the table of stand_window is what the engine does, for every pair of surfaces and the four steps
//   AI59.2  a hurt worker with an enemy that strikes it walks beside a power-up and stands on it: the enemy's blows land on nothing, the ant keeps its hit points; with the rule off it dies
//   AI59.3  the refuge is left when the enemy is gone: the ant walks home and is whole again
//   AI59.4  a power-up that the plan wants is taken by the ant that stands on it (a click on its own tile); with plan.stand_take off the ant stays on it
//   AI59.5  an ant that the economy has no work for stands on a power-up that an enemy is about to take: the enemy cannot take it; with the rule off it does
//   AI59.6  cancel_tile: a tile that no walk of the ant's part of the map comes within four tiles of, none on an open field
//   AI59.7  whole matches on TREASURE and ISLANDS with the rule on: they replay from their commands alone, the stands that were tried held (the plans of every level have the rule off)
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ai_test.hpp"
#include "ants_ai/arena.hpp"
#include "ants_ai/bot_controller.hpp"
#include "ants_ai/power_stand.hpp"
#include "ants_ai/standard_bot.hpp"
#include "b41_helpers.hpp"
#include "contest_world.hpp"

namespace {

using namespace contest_world;

LevelPlan plan_stand(const std::function<void(LevelPlan&)>& tweak = {}) {
    LevelPlan p = peace_plan(Level::Hard);
    p.power_stand = true;
    p.stand_refuge = true;
    p.stand_deny = false;
    p.stand_take = true;
    if (tweak) tweak(p);
    return p;
}

}  // namespace

void run_powerstand_tests() {
    TEST_CASE("AI59.1 The Window Of The Cancel: The Table Of stand_window Is What The Engine Does, For Every Pair Of Surfaces And The Four Steps (The First Tick At Which An Order Given In The Same Way Keeps The Ant On The Power-Up To The Last)") {
        const sim::SurfaceType kinds[4] = {sim::SurfaceType::Grass, sim::SurfaceType::Mud, sim::SurfaceType::Gravel, sim::SurfaceType::Slate};
        const int steps[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
        for (const sim::SurfaceType from : kinds) {
            for (const sim::SurfaceType to : kinds) {
                for (const auto& step : steps) {
                    int lo = -1, hi = -1;
                    for (int gap = 2; gap <= 30; ++gap) {
                        sim::SimulationEngine sim;
                        sim.init_test_world(60, 60, 1, 720000);
                        sim.set_anthill(0, TileCoord{2, 2});
                        sim.set_anthill(1, TileCoord{50, 50});
                        for (int y = 29; y <= 31; ++y) {
                            for (int x = 29; x <= 31; ++x) {
                                if (x != 30 || y != 30) sim.grid_mut().get_cell_mut(static_cast<uint32_t>(x), static_cast<uint32_t>(y)).terrain_type = sim::TERRAIN_WATER;       // (an island at (30, 30): the cancel's click finds no path)
                            }
                        }
                        const TileCoord a{20, 20};
                        const TileCoord p{20 + step[0], 20 + step[1]};
                        sim.grid_mut().get_cell_mut(static_cast<uint32_t>(a.x), static_cast<uint32_t>(a.y)).surface_type = from;
                        sim.grid_mut().get_cell_mut(static_cast<uint32_t>(p.x), static_cast<uint32_t>(p.y)).surface_type = to;
                        sim.grid_mut().place_powerup(p.x, p.y, 4);
                        const uint32_t ant = sim.spawn_unit(0, sim::AntType::Worker, a);
                        sim.apply_command(command_of(CommandType::GroupMove, 0, {ant}, p.x, p.y));
                        for (int i = 0; i < gap; ++i) sim.tick();
                        sim.apply_command(command_of(CommandType::GroupMove, 0, {ant}, 30, 30));
                        for (int i = 0; i < 80; ++i) sim.tick();
                        const sim::AntSnapshot* u = snapshot_of(sim, ant);
                        const bool stands = u != nullptr && u->raw_type == sim::AntType::Worker && sim.grid().has_powerup_at(p) && u->tile_x == p.x && u->tile_y == p.y;
                        if (stands) {
                            if (lo < 0) lo = gap;
                            hi = gap;
                        }
                    }
                    const StandWindow w = stand_window(from, to, step[0] < 0 || step[1] < 0);
                    ASSERT_EQ(lo, static_cast<int>(w.lo));
                    ASSERT_EQ(hi, static_cast<int>(w.hi));
                }
            }
        }
        ASSERT_EQ(static_cast<int>(stand_window(sim::SurfaceType::Water, sim::SurfaceType::Grass, false).hi), 0);       // (water is no surface of a power-up)
    } TEST_END();

    TEST_CASE("AI59.2 A Hurt Worker With An Enemy Combat Ant That Strikes It Walks Beside A Power-Up Three Tiles Away And Stands On It: Every Blow Lands On Nothing, The Ant Keeps Its Hit Points And The Power-Up Is Still There; With The Rule Off The Ant Dies") {
        struct Out {
            bool alive{false};
            bool standing{false};
            uint16_t hp{0};
            bool power_up{false};
            uint32_t held{0};
        };
        const auto play = [&](bool rule) {
            World w(plan_stand([rule](LevelPlan& p) { p.power_stand = rule; }));
            const TileCoord power{33, 30};
            w.sim.grid_mut().place_powerup(power.x, power.y, 5);                                 // (a Swimmer power-up: the plan has no use for one on this field, so no trip is made to it)
            const uint32_t ant = w.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{30, 30});
            w.sim.get_unit(ant).hp = 5;
            const uint32_t enemy = w.sim.spawn_unit(1, sim::AntType::Combat, TileCoord{27, 30});
            w.run(900, [&]() { keep_attacking(w.sim, 1, enemy, ant); });
            Out out;
            out.alive = alive(w.sim, ant);
            out.hp = hp_of(w.sim, ant);
            out.power_up = w.sim.grid().has_powerup_at(power);
            out.standing = out.alive && on_tile(w.sim, ant, power);
            out.held = w.bot().power_stand().held();
            return out;
        };
        const Out off = play(false);
        ASSERT_FALSE(off.alive);                                                                 // the Combat Ant's punches (2 hit points each) kill a worker that has 5
        const Out on = play(true);
        ASSERT_TRUE(on.alive && on.standing);
        ASSERT_TRUE(on.power_up);                                                                // the ant stands on it, it did not take it
        ASSERT_EQ(on.held, 1u);
        ASSERT_TRUE(on.hp >= 1u && on.hp <= 5u);
    } TEST_END();

    TEST_CASE("AI59.3 The Refuge Is Left When The Enemy Is Gone: The Ant Walks Home Into Its Hill And Is Whole Again") {
        World w(plan_stand());
        const TileCoord power{20, 12};
        w.sim.grid_mut().place_powerup(power.x, power.y, 5);
        const uint32_t ant = w.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{18, 12});
        w.sim.get_unit(ant).hp = 4;
        const uint32_t enemy = w.sim.spawn_unit(1, sim::AntType::Combat, TileCoord{15, 12});
        bool stood = false;
        bool enemy_sent_away = false;
        w.run(1800, [&]() {
            const sim::AntSnapshot* a = snapshot_of(w.sim, ant);
            if (a != nullptr && a->tile_x == power.x && a->tile_y == power.y && w.bot().power_stand().standing_now() == 1) stood = true;
            if (!stood || !alive(w.sim, ant)) {
                keep_attacking(w.sim, 1, enemy, ant);
            } else if (!enemy_sent_away) {                                                       // (it stands: the enemy gives up and walks off to the far corner)
                w.sim.apply_command(command_of(CommandType::GroupMove, 1, {enemy}, 55, 55));
                enemy_sent_away = true;
            }
        });
        ASSERT_TRUE(stood);
        ASSERT_TRUE(alive(w.sim, ant));
        ASSERT_EQ(w.bot().power_stand().standing_now(), 0u);                                     // (given up after the calm: the ledger is free)
        ASSERT_EQ(w.bot().ledger().owner(ant), kNoTask);
        ASSERT_EQ(hp_of(w.sim, ant), 10u);                                                       // the hill's door heals
        ASSERT_TRUE(w.sim.grid().has_powerup_at(power));                                         // (and it never took the power-up)
    } TEST_END();

    TEST_CASE("AI59.4 A Power-Up That The Plan Wants Is Taken By The Ant That Stands On It, When The Enemy Is Gone: A Click On Its Own Tile (The Hurt Worker Becomes The Fire Ant); While The Enemy Is Near It Stays On It, Immune; With stand_take Off It Stays On It For Good") {
        for (const int mode : {0, 1, 2}) {                                                       // 0: the enemy stays and strikes, 1: the enemy leaves, 2: the enemy leaves and stand_take is off
            World w(plan_stand([mode](LevelPlan& p) { p.stand_take = mode != 2; }));
            const TileCoord power{16, 12};
            w.sim.grid_mut().place_powerup(power.x, power.y, 2);                                 // a Fire power-up on the own side of the field: the Hard plan wants a Fire Ant for it
            const uint32_t ant = w.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{14, 12});
            w.sim.get_unit(ant).hp = 4;                                                          // (hurt: no trip to a power-up takes an ant with fewer than 5 hit points, so the stand comes first)
            const uint32_t enemy = w.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{11, 12});
            w.run(1200, [&]() {
                const bool stood = w.bot().power_stand().held() >= 1u;
                if (mode == 0 || !stood) keep_attacking(w.sim, 1, enemy, ant);
                else if (w.sim.current_tick() % 20 == 0) {                                       // (it gives up and walks off to the far corner: told again until it walks)
                    const sim::AntSnapshot* e = snapshot_of(w.sim, enemy);
                    if (e != nullptr && e->tile_x < 40) w.sim.apply_command(command_of(CommandType::GroupMove, 1, {enemy}, 55, 55));
                }
            });
            const sim::AntSnapshot* a = snapshot_of(w.sim, ant);
            ASSERT_TRUE(a != nullptr && alive(w.sim, ant));
            if (mode == 1) {
                ASSERT_TRUE(a->raw_type == sim::AntType::Fire);                                  // it took the power-up after the calm
                ASSERT_FALSE(w.sim.grid().has_powerup_at(power));
                ASSERT_EQ(w.bot().power_stand().taken(), 1u);
            } else if (mode == 0) {
                ASSERT_TRUE(a->raw_type == sim::AntType::Worker);                                // the enemy near: it stands, immune, and the power-up is there
                ASSERT_TRUE(w.sim.grid().has_powerup_at(power));
                ASSERT_EQ(w.bot().power_stand().held(), 1u);
            } else {
                ASSERT_EQ(w.bot().power_stand().taken(), 0u);                                    // no click on its own tile (the ant healed at its hill and the pick-up trip took the power-up later)
                ASSERT_EQ(w.bot().power_stand().held(), 1u);
            }
        }
    } TEST_END();

    TEST_CASE("AI59.5 An Ant That The Economy Has No Work For Stands On A Swimmer Power-Up That An Enemy Worker Is About To Take: The Enemy Is Ordered Onto It And Cannot Take It; Without The Deny The Enemy Takes It") {
        for (const bool deny : {true, false}) {
            World w(plan_stand([deny](LevelPlan& p) { p.stand_refuge = false; p.stand_deny = deny; p.stand_deny_calm = 100000; p.stand_deny_kinds = 0x3E; }));
            const TileCoord power{30, 20};
            w.sim.grid_mut().place_powerup(power.x, power.y, 5);                                 // (a Swimmer power-up: nothing the plan wants, so it is not the bot's to take)
            const uint32_t spare = w.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{27, 20});         // no pile on the field: the economy has nothing for it
            const uint32_t enemy = w.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{35, 20});
            w.run(60);
            w.sim.apply_command(command_of(CommandType::GroupMove, 1, {enemy}, power.x, power.y));    // the enemy's click on the power-up
            w.run(800, [&]() {
                const sim::AntSnapshot* e = snapshot_of(w.sim, enemy);
                if (e != nullptr && e->state == sim::UnitState::Idle && !w.sim.has_pending_path(enemy) && w.sim.current_tick() % 40 == 0) w.sim.apply_command(command_of(CommandType::GroupMove, 1, {enemy}, power.x, power.y));
            });
            const sim::AntSnapshot* e = snapshot_of(w.sim, enemy);
            ASSERT_TRUE(e != nullptr);
            if (deny) {
                ASSERT_TRUE(e->raw_type == sim::AntType::Worker);                                // nobody takes it from the ant that stands on it
                ASSERT_TRUE(w.sim.grid().has_powerup_at(power));
                ASSERT_TRUE(on_tile(w.sim, spare, power));
                ASSERT_TRUE(w.bot().power_stand().stands(PowerStandTask::Why::Deny) >= 1u);
            } else {
                ASSERT_TRUE(e->raw_type == sim::AntType::Swimmer);
                ASSERT_FALSE(w.sim.grid().has_powerup_at(power));
            }
        }
    } TEST_END();

    TEST_CASE("AI59.6 The Tile Of The Cancel: One That No Walk Of The Ant's Part Of The Map Comes Within Four Tiles Of (Its Click Is Accepted And Goes Nowhere); None On An Open Field") {
        {
            sim::SimulationEngine sim;
            empty_field(sim, 3);
            const MapInfo map(sim);
            const BotView view = BotView::build(sim, 0, &map);
            const sim::TileCoord none = cancel_tile(view, view.walkable_mask(0), TileCoord{20, 20});
            ASSERT_EQ(none.x, -1);                                                               // the whole field is one part: no tile lies four tiles from it
        }
        {
            sim::SimulationEngine sim;
            empty_field(sim, 3);
            add_pocket(sim);
            const MapInfo map(sim);
            const BotView view = BotView::build(sim, 0, &map);
            const sim::TileCoord t = cancel_tile(view, view.walkable_mask(0), TileCoord{20, 20});
            ASSERT_EQ(t.x, 40);                                                                  // the first tile of the pocket in reading order: a tile of no walk of the ant's part
            ASSERT_EQ(t.y, 40);
        }
        {
            sim::SimulationEngine sim;
            start_match(sim, "ISLANDS", 1, 0x01);
            const MapInfo map(sim);
            const BotView view = BotView::build(sim, 0, &map);
            const std::vector<uint8_t>& mask = view.walkable_mask(0);
            const TileCoord from{map.hill(0).entrance.x, map.hill(0).entrance.y + 4};
            ASSERT_TRUE(mask[static_cast<size_t>(from.y) * view.grid().width() + static_cast<size_t>(from.x)] != 0);
            const sim::TileCoord t = cancel_tile(view, mask, from);
            ASSERT_TRUE(t.x >= 0);
            // brute force: no tile of the walk from `from` is within four tiles of t
            std::vector<uint8_t> part(mask.size(), 0);
            std::vector<int32_t> stack{from.y * static_cast<int32_t>(view.grid().width()) + from.x};
            part[static_cast<size_t>(stack[0])] = 1;
            const int32_t w = static_cast<int32_t>(view.grid().width());
            const int32_t h = static_cast<int32_t>(view.grid().height());
            while (!stack.empty()) {
                const int32_t i = stack.back();
                stack.pop_back();
                for (int32_t dy = -1; dy <= 1; ++dy) {
                    for (int32_t dx = -1; dx <= 1; ++dx) {
                        const int32_t x = i % w + dx;
                        const int32_t y = i / w + dy;
                        if (x < 0 || y < 0 || x >= w || y >= h || mask[static_cast<size_t>(y * w + x)] == 0 || part[static_cast<size_t>(y * w + x)] != 0) continue;
                        part[static_cast<size_t>(y * w + x)] = 1;
                        stack.push_back(y * w + x);
                    }
                }
            }
            for (int32_t y = std::max(0, t.y - 4); y <= std::min(h - 1, t.y + 4); ++y) {
                for (int32_t x = std::max(0, t.x - 4); x <= std::min(w - 1, t.x + 4); ++x) ASSERT_EQ(part[static_cast<size_t>(y * w + x)], 0);
            }
            ASSERT_TRUE(view.powerup_at(t) == nullptr);
        }
    } TEST_END();

    TEST_CASE("AI59.7 The Plans That Ship Have The Rule Off, And Whole Matches With It On Replay From Their Commands Alone (Four Hard Bots On TREASURE And On ISLANDS, With Every Stand Rule On)") {
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) ASSERT_FALSE(plan_for(level).power_stand);
        const LevelPlan by_hand;
        ASSERT_FALSE(by_hand.power_stand);
        for (const char* name : {"TREASURE", "ISLANDS"}) {
            assets::LevelData level;
            ASSERT_TRUE(level.load_from_file(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/" + name + ".LVL"));
            ArenaSpec spec;
            spec.level = &level;
            spec.seed = 3;
            spec.record = true;
            for (uint8_t seat = 0; seat < 4; ++seat) {
                BotSpec b;
                b.seat = seat;
                b.level = Level::Hard;
                spec.bots.push_back(b);
            }
            spec.factory = [](const BotSpec& b) {
                return std::unique_ptr<Bot>(std::make_unique<StandardBot>(b.level, Style::Random, [](LevelPlan& plan) {
                    plan.power_stand = plan.stand_refuge = plan.stand_deny = plan.stand_take = true;
                }));
            };
            const ArenaResult r = play_match(spec);
            ASSERT_TRUE(r.error.empty());
            ASSERT_TRUE(replay_commands(spec, r).ok);
            for (const ArenaSeatResult& seat : r.seats) {
                ASSERT_EQ(seat.stats.filtered, 0u);                                                    // (no click that a person could not give: the cancel is a plain move)
                ASSERT_EQ(seat.stats.rejected, 0u);
            }
        }
    } TEST_END();
}
