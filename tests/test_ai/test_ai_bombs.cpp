// AI51: bombs on the way and far food (docs/BOTS.md, "Bombs on the way"; docs/BOT_FOOD.md): a standard bot walks round the bombs of other teams that it sees, a Bomber defuses the bomb
// that it can reach over a clear walk and that shuts a route, and a pile that a nearer enemy eats first is left alone while another pile has food. Hand-made worlds on the 60 x 60 field
// of b41_helpers.hpp (four hills, grass, nothing else): seat 0's hill is at (4, 4); the bombs are team 1's. The engine itself is the yardstick: a walk that goes over a bomb sets it off.
//
//   AI51.1  a worker's walk to a pile with a bomb of another team on the straight way is sent to ONE waypoint first and the order is given again there (the bomb is still there and the worker
//           is whole at the pile); with the detour off the same match sets the bomb off. The tasks are told the fate of the order they proposed, never of the waypoint
//   AI51.2  an order whose walk meets no bomb is left byte for byte as it was: none on the map, an own bomb, a bomb far from the walk, a click onto a bomb (a set-off or a defuse on purpose)
//   AI51.3  the levels: Easy never, Medium the walks of workers, power-up trips and fights (not a Thief, not a special order), Hard every walk (a raid, a mine trip); a Swimmer never; a walk with no
//           way round (a bomb in a corridor of rocks) goes as it is; a detour that makes the walk much longer is not taken
//   AI51.4  a bomb that appears on a walk under way sends the ant round it at the next look, and a Hard carrier that walks home on the engine's own order is sent round a bomb on its way
//   AI51.5  a Bomber that the engine's walk would take over another bomb walks to a tile beside the bomb that holds none first; with broute=0 it is sent straight and is hurt
//   AI51.6  a bomb in a corridor of rocks (no way round) on the way to a pile is defused by a Bomber that is far from it, over no bomb; without the rule (broute=0) nobody clears it
//   AI51.7  a pile of the class Hopeless (an enemy's hill is far nearer) is left alone while another pile has food (nohope=0 sends ants there)
//   AI51.8  the plans and the tune keys
//   AI51.10 the order that is given again at the waypoint of a power-up trip keeps its pick-up mark (the controller lets a move onto a power-up through only with it)
//   AI51.11 a group that is sent round a bomb is never one command of more than 32 ants (the controller refuses a bigger one whole)
//   AI51.12 the Hopeless rule applies only while another pile has food: when every pile is Hopeless the ants go to them as before
//   AI51.9  whole matches on TREASURE (Hard, seeds 1 to 6, with and without the detour and the Bomber's route): fewer bombs walked into, and the score within 3 percent
#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ai_test.hpp"
#include "ants_ai/arena.hpp"
#include "ants_ai/bomb_detour.hpp"
#include "ants_ai/standard_bot.hpp"
#include "b41_helpers.hpp"
#include "bot_arena_tune.hpp"

namespace {

using namespace ai_test;
using namespace ants;
using namespace ants::ai;
using namespace b41;

// The orders of seat 0 for one look, after BombDetour has seen them
struct Look {
    std::vector<Intent> intents;
};

Look judge(BombDetour& detour, sim::SimulationEngine& sim, Level level, const std::function<void(Orders&)>& give, const std::function<void(LevelPlan&)>& tune = nullptr) {
    MapInfo map(sim);
    const BotView view = BotView::build(sim, 0, &map);
    LevelPlan plan = plan_for(level);
    if (tune) tune(plan);
    Orders orders;
    give(orders);
    detour.apply(view, plan, profile_for(level), orders);
    Look look;
    look.intents = orders.intents();
    return look;
}

bool same_orders(const std::vector<Intent>& a, const Orders& b) {
    if (a.size() != b.intents().size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const Command& x = a[i].command;
        const Command& y = b.intents()[i].command;
        if (x.type != y.type || x.tile_x != y.tile_x || x.tile_y != y.tile_y || x.ants != y.ants || a[i].priority != b.intents()[i].priority || a[i].pickup != b.intents()[i].pickup) return false;
    }
    return true;
}

// A field with a worker at (10, 20), a pile at (30, 20) and a bomb of team 1 at (20, 20): the straight walk of 20 tiles crosses it
struct Road {
    uint32_t worker{0};
    void build(sim::SimulationEngine& sim, uint32_t seed = 71) {
        empty_field(sim, seed);
        add_pile(sim, 30, 20, 40, 30);
        sim.grid_mut().place_bomb(20, 20, 1);
        worker = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{10, 20});
    }
};

// The bombs of other teams that were walked into in a match: a bomb that vanishes with an ant of another team on its tile a tick before (a defuse leaves no ant there)
struct BlastCount {
    std::vector<int16_t> owner;
    std::vector<std::pair<size_t, uint8_t>> ants;
    uint32_t blasts{0};
    void scan(const sim::SimulationEngine& sim) {
        const sim::Grid& g = sim.grid();
        const size_t w = g.width();
        std::vector<int16_t> now(w * g.height(), -1);
        for (uint32_t y = 0; y < g.height(); ++y) {
            for (uint32_t x = 0; x < w; ++x) {
                const sim::TileCell& c = g.get_cell(sim::TileCoord{static_cast<int32_t>(x), static_cast<int32_t>(y)});
                if (c.has_bomb()) now[y * w + x] = static_cast<int16_t>(c.interactive_owner);
            }
        }
        if (owner.size() == now.size()) {
            for (size_t i = 0; i < now.size(); ++i) {
                if (owner[i] < 0 || now[i] >= 0) continue;
                for (const auto& a : ants) {
                    if (a.first == i && a.second != owner[i]) {
                        ++blasts;
                        break;
                    }
                }
            }
        }
        owner = std::move(now);
        ants.clear();
        for (const sim::AntSnapshot& a : sim.get_world_state().ants) {
            if (a.hp > 0 && a.tile_x >= 0 && a.tile_y >= 0) ants.emplace_back(static_cast<size_t>(a.tile_y) * w + static_cast<size_t>(a.tile_x), a.player_id);
        }
    }
};

bool is_move_to(const Intent& in, int x, int y) { return in.command.type == CommandType::GroupMove && in.command.tile_x == x && in.command.tile_y == y; }

}  // namespace

void run_bombs_tests() {
    TEST_CASE("AI51.1 A Worker's Walk To A Pile With A Bomb Of Another Team On The Straight Way Goes To One Waypoint First And The Order Is Given Again There; The Bomb Stays And The Worker Is Whole (With The Detour Off The Same Match Sets The Bomb Off)") {
        for (const Level level : {Level::Medium, Level::Hard}) {
            for (const bool detour : {true, false}) {
                sim::SimulationEngine sim;
                Road road;
                road.build(sim);
                LevelPlan p = peace_plan(level);
                p.bomb_detour = detour ? p.bomb_detour : 0;
                Rig rig(sim, 0, level, std::make_unique<StandardBot>(p), 4, 4);
                bool beyond = false;
                for (int step = 0; step < 70; ++step) {
                    rig.run(10);
                    for (const sim::AntSnapshot& a : sim.get_world_state().ants) beyond = beyond || (a.id == road.worker && a.tile_x > 24);
                }
                const StandardBot& bot = rig.as<StandardBot>();
                if (detour) {
                    ASSERT_TRUE(sim.grid().has_bomb_at(TileCoord{20, 20}));
                    ASSERT_EQ(sim.get_unit(road.worker).hp, 10);
                    ASSERT_TRUE(bot.detour().detours() >= 1u);
                    ASSERT_TRUE(bot.detour().reissued() >= 1u);
                    ASSERT_TRUE(beyond);                                                         // (the ant went on past the bomb and reached the pile)
                    size_t waypoint_orders = 0;
                    for (const auto& e : rig.proposed) {
                        if (e.second.type == CommandType::GroupMove && !(e.second.tile_x == 30 && e.second.tile_y == 20) && !(e.second.tile_x > 28 && e.second.tile_x < 33)) ++waypoint_orders;
                    }
                    ASSERT_TRUE(waypoint_orders >= 1u);
                } else {
                    ASSERT_FALSE(sim.grid().has_bomb_at(TileCoord{20, 20}));                      // the walk went over it
                    ASSERT_EQ(bot.detour().detours(), 0u);
                }
            }
        }
    } TEST_END();

    TEST_CASE("AI51.2 An Order Whose Walk Meets No Bomb Of Another Team Is Left Byte For Byte As It Was: No Bomb, An Own Bomb, A Bomb Far From The Walk, A Click Onto A Bomb") {
        for (const int variant : {0, 1, 2, 3}) {
            sim::SimulationEngine sim;
            empty_field(sim, 72);
            add_pile(sim, 30, 20, 40, 30);
            const uint32_t w = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{10, 20});
            sim.spawn_unit(1, sim::AntType::Worker, TileCoord{40, 40});
            if (variant == 1) sim.grid_mut().place_bomb(20, 20, 0);                              // an own bomb: the engine's walk goes round it already
            if (variant == 2) sim.grid_mut().place_bomb(20, 56, 1);                              // a bomb of another team, far from the walk
            if (variant == 3) sim.grid_mut().place_bomb(20, 20, 1);
            MapInfo map(sim);
            const BotView view = BotView::build(sim, 0, &map);
            Orders orders;
            if (variant == 3) orders.move({w}, TileCoord{20, 20});                                // (a click onto the bomb itself: a set-off on purpose)
            else orders.move({w}, TileCoord{30, 20});
            Orders before = orders;
            BombDetour detour;
            LevelPlan plan = plan_for(Level::Hard);
            detour.apply(view, plan, profile_for(Level::Hard), orders);
            ASSERT_TRUE(same_orders(before.intents(), orders));
            ASSERT_EQ(detour.detours(), 0u);
        }
    } TEST_END();

    TEST_CASE("AI51.3 The Levels: Easy Never; Medium The Walks Of Workers (Not A Thief, Not A Special Order); Hard Every Walk; A Swimmer Never; No Way Round Or A Much Longer One: The Walk Goes As It Is") {
        // one ant at (10, 20) on the road of AI51.1 and one order: kind 0 a worker's move, 1 a thief's move, 2 a Bomber's special order, 3 a Swimmer's move, 4 a Combat Ant's attack
        const auto detoured = [&](Level level, int kind) {
            sim::SimulationEngine sim;
            empty_field(sim, 71);
            add_pile(sim, 30, 20, 40, 30);
            sim.grid_mut().place_bomb(20, 20, 1);
            const sim::AntType types[5] = {sim::AntType::Worker, sim::AntType::Thief, sim::AntType::Bomber, sim::AntType::Swimmer, sim::AntType::Combat};
            const uint32_t ant = sim.spawn_unit(0, types[kind], TileCoord{10, 20});
            sim.spawn_unit(1, sim::AntType::Worker, TileCoord{30, 19});
            BombDetour d;
            const Look look = judge(d, sim, level, [&](Orders& o) {
                if (kind == 2) o.special(ant, TileCoord{31, 20});
                else if (kind == 4) o.attack({ant}, TileCoord{30, 19});
                else o.move({ant}, TileCoord{30, 20});
            });
            return d.detours() == 1u && look.intents.size() == 1 && look.intents[0].command.tile_x < 29;
        };
        const bool expect[3][5] = {{false, false, false, false, false}, {true, false, false, false, true}, {true, true, true, false, true}};
        const Level levels[3] = {Level::Easy, Level::Medium, Level::Hard};
        for (int l = 0; l < 3; ++l) {
            for (int k = 0; k < 5; ++k) ASSERT_EQ(detoured(levels[l], k), expect[l][k]);
        }
        // no way round: the bomb stands in a corridor of rocks (a wall across the field with a gap of one tile)
        {
            sim::SimulationEngine corridor;
            empty_field(corridor, 73);
            add_pile(corridor, 30, 20, 40, 30);
            for (int y = 0; y < 60; ++y) {
                if (y != 20) corridor.set_terrain(20, y, sim::TERRAIN_OBSTACLE);
            }
            corridor.grid_mut().place_bomb(20, 20, 1);
            const uint32_t w = corridor.spawn_unit(0, sim::AntType::Worker, TileCoord{10, 20});
            BombDetour d;
            const Look look = judge(d, corridor, Level::Hard, [&](Orders& o) { o.move({w}, TileCoord{30, 20}); });
            ASSERT_EQ(look.intents.size(), size_t{1});
            ASSERT_TRUE(is_move_to(look.intents[0], 30, 20));
            ASSERT_EQ(d.detours(), 0u);
        }
        // a much longer way round (a wall with a gap 40 tiles off): not taken at 100 percent, taken when the limit is lifted
        {
            sim::SimulationEngine wall;
            empty_field(wall, 74);
            add_pile(wall, 30, 20, 40, 30);
            for (int y = 1; y < 59; ++y) {
                if (y != 20) wall.set_terrain(20, y, sim::TERRAIN_OBSTACLE);
            }
            wall.grid_mut().place_bomb(20, 20, 1);
            const uint32_t w = wall.spawn_unit(0, sim::AntType::Worker, TileCoord{10, 20});
            BombDetour d;
            const Look cut = judge(d, wall, Level::Hard, [&](Orders& o) { o.move({w}, TileCoord{30, 20}); });
            ASSERT_EQ(cut.intents.size(), size_t{1});
            ASSERT_TRUE(is_move_to(cut.intents[0], 30, 20));
            ASSERT_TRUE(d.refused_long() >= 1u);
            BombDetour d2;
            const Look lifted = judge(d2, wall, Level::Hard, [&](Orders& o) { o.move({w}, TileCoord{30, 20}); }, [](LevelPlan& p) { p.detour_extra_percent = 10000; });
            ASSERT_EQ(lifted.intents.size(), size_t{1});
            ASSERT_FALSE(is_move_to(lifted.intents[0], 30, 20));
        }
    } TEST_END();

    TEST_CASE("AI51.4 A Bomb That Appears On A Walk Under Way Sends The Ant Round It At The Next Look; A Hard Carrier That Walks Home On The Engine's Own Order Is Sent Round A Bomb On Its Way (Medium's Is Not)") {
        for (const Level level : {Level::Medium, Level::Hard}) {
            sim::SimulationEngine sim;
            empty_field(sim, 75);
            add_pile(sim, 40, 20, 40, 30);
            const uint32_t w = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{10, 20});
            BombDetour d;
            {
                const Look first = judge(d, sim, level, [&](Orders& o) { o.move({w}, TileCoord{40, 20}); });
                ASSERT_EQ(first.intents.size(), size_t{1});
                ASSERT_TRUE(is_move_to(first.intents[0], 40, 20));
            }
            sim.apply_command(command_of(CommandType::GroupMove, 0, {w}, 40, 20));
            tick_all(sim, 20);
            sim.grid_mut().place_bomb(30, 20, 1);                                                 // a bomb of another team lands on the way
            const Look second = judge(d, sim, level, [](Orders&) {});
            ASSERT_EQ(second.intents.size(), size_t{1});
            ASSERT_EQ(second.intents[0].command.ants.size(), size_t{1});
            ASSERT_FALSE(is_move_to(second.intents[0], 30, 20));
            ASSERT_TRUE(second.intents[0].command.tile_x > 11);
        }
        for (const Level level : {Level::Medium, Level::Hard}) {
            sim::SimulationEngine sim;
            empty_field(sim, 76);
            const uint32_t carrier = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{40, 3});
            sim.get_unit(carrier).holding = 1;
            sim.get_unit(carrier).carried_points = 20;
            sim.grid_mut().place_bomb(25, 3, 1);
            sim.apply_command(command_of(CommandType::GroupMove, 0, {carrier}, 5, 5));            // (the engine's own walk home)
            tick_all(sim, 10);
            BombDetour d;
            const Look look = judge(d, sim, level, [](Orders&) {});
            if (level == Level::Hard) {
                ASSERT_EQ(look.intents.size(), size_t{1});
                ASSERT_EQ(look.intents[0].command.type, CommandType::GroupMove);
                ASSERT_EQ(look.intents[0].command.ants.size(), size_t{1});
                ASSERT_EQ(d.carriers(), 1u);
            } else {
                ASSERT_TRUE(look.intents.empty());
            }
        }
    } TEST_END();

    TEST_CASE("AI51.10 The Order That Is Given Again At The Waypoint Of A Power-Up Trip Keeps Its Pick-Up Mark: A Plain Click On A Power-Up Tile Is Refused By The Controller") {
        sim::SimulationEngine sim;
        empty_field(sim, 80);
        sim.grid_mut().place_powerup(40, 20, 3);
        sim.grid_mut().place_bomb(25, 20, 1);
        const uint32_t w = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{10, 20});
        const auto tile_of = [&](uint32_t id) {
            for (const sim::AntSnapshot& a : sim.get_world_state().ants) {
                if (a.id == id) return TileCoord{a.tile_x, a.tile_y};
            }
            return TileCoord{-1, -1};
        };
        BombDetour d;
        Look look = judge(d, sim, Level::Hard, [&](Orders& o) { o.pick_up(w, TileCoord{40, 20}); });
        int legs = 0;
        while (legs < 8 && !(look.intents.size() == 1 && is_move_to(look.intents[0], 40, 20))) {            // (the waypoints: plain moves, one after the other, until the walk to the power-up meets no bomb)
            ASSERT_EQ(look.intents.size(), size_t{1});
            ASSERT_FALSE(look.intents[0].pickup);
            const Command waypoint = look.intents[0].command;
            sim.apply_command(command_of(CommandType::GroupMove, 0, {w}, waypoint.tile_x, waypoint.tile_y));
            d.note(waypoint, Bot::Fate::Sent, sim.current_tick() + 1);
            for (int i = 0; i < 100 && tile_of(w).chebyshev_dist(TileCoord{waypoint.tile_x, waypoint.tile_y}) > 0; ++i) tick_all(sim, 5);
            tick_all(sim, 10);
            look = judge(d, sim, Level::Hard, [](Orders&) {});
            ++legs;
        }
        ASSERT_TRUE(legs >= 1);
        ASSERT_EQ(look.intents.size(), size_t{1});
        ASSERT_TRUE(is_move_to(look.intents[0], 40, 20));
        ASSERT_TRUE(look.intents[0].pickup);
        ASSERT_EQ(d.reissued(), 1u);
    } TEST_END();

    TEST_CASE("AI51.11 A Group Sent Round A Bomb Is Never One Command Of More Than 32 Ants: Forty Workers On Two Orders Are Sent Round A Wall Of Bombs That Appears On Their Way, And Every Command That Leaves Names 32 At Most") {
        sim::SimulationEngine sim;
        empty_field(sim, 81);
        add_pile(sim, 40, 17, 80, 30);
        std::vector<uint32_t> ants;
        for (int i = 0; i < 40; ++i) ants.push_back(sim.spawn_unit(0, sim::AntType::Worker, TileCoord{14 + i % 8, 15 + i / 8}));
        const std::vector<uint32_t> first_half(ants.begin(), ants.begin() + 20);
        const std::vector<uint32_t> second_half(ants.begin() + 20, ants.end());
        BombDetour d;
        {
            const Look first = judge(d, sim, Level::Hard, [&](Orders& o) {
                o.move(first_half, TileCoord{40, 17});
                o.move(second_half, TileCoord{40, 17});
            });
            ASSERT_EQ(first.intents.size(), size_t{2});                                           // (no bomb yet: both orders go as they are)
        }
        sim.apply_command(command_of(CommandType::GroupMove, 0, first_half, 40, 17));
        sim.apply_command(command_of(CommandType::GroupMove, 0, second_half, 40, 17));
        tick_all(sim, 12);
        for (int y = 14; y <= 26; ++y) sim.grid_mut().place_bomb(28, static_cast<uint32_t>(y), 1);          // a wall of bombs of another team lands across the way, with a way round at both ends
        const Look second = judge(d, sim, Level::Hard, [](Orders&) {});
        size_t named = 0;
        for (const Intent& in : second.intents) {
            ASSERT_TRUE(in.command.ants.size() <= sim::kMaxCommandAnts);
            named += in.command.ants.size();
        }
        ASSERT_EQ(named, size_t{40});
    } TEST_END();

    TEST_CASE("AI51.5 A Bomber That The Engine's Walk Would Take Over Another Bomb Walks To A Tile Beside The Bomb That Holds None First: Whole And The Bomb Gone; With broute=0 It Is Sent Straight And Is Hurt") {
        for (const bool route : {true, false}) {
            sim::SimulationEngine sim;
            empty_field(sim, 77);
            sim.grid_mut().place_bomb(9, 12, 1);                                                 // 8 tiles from the hill: harmful
            sim.grid_mut().place_bomb(20, 12, 1);                                                 // between the Bomber and it, 16 tiles from the hill: not harmful, but on the way
            const uint32_t bomber = sim.spawn_unit(0, sim::AntType::Bomber, TileCoord{28, 12});
            LevelPlan p = peace_plan(Level::Medium);
            p.bomb_route = route;
            Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(p), 4, 4);
            rig.run(500);
            const StandardBot& bot = rig.as<StandardBot>();
            if (route) {
                ASSERT_FALSE(sim.grid().has_bomb_at(TileCoord{9, 12}));
                ASSERT_TRUE(sim.grid().has_bomb_at(TileCoord{20, 12}));
                ASSERT_EQ(sim.get_unit(bomber).hp, 10);
                ASSERT_EQ(bot.bombs().side_walks(), 1u);
            } else {
                ASSERT_FALSE(sim.grid().has_bomb_at(TileCoord{20, 12}));
                ASSERT_TRUE(sim.get_unit(bomber).hp < 10);
            }
        }
    } TEST_END();

    TEST_CASE("AI51.6 A Bomb In A Corridor Of Rocks (No Way Round) On The Way To A Pile Is Defused By A Bomber That Is Far From It, Over No Bomb; Without The Rule (broute=0) Nobody Clears It") {
        for (const bool route : {true, false}) {
            sim::SimulationEngine sim;
            empty_field(sim, 78);
            add_pile(sim, 30, 20, 60, 30);
            for (int y = 0; y < 60; ++y) {
                if (y != 20) sim.set_terrain(20, y, sim::TERRAIN_OBSTACLE);
            }
            sim.grid_mut().place_bomb(20, 20, 1);
            const uint32_t bomber = sim.spawn_unit(0, sim::AntType::Bomber, TileCoord{14, 20});
            for (int i = 0; i < 3; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{8 + i, 10});
            LevelPlan p = peace_plan(Level::Medium);
            p.bomb_route = route;
            p.bomb_detour = 0;
            p.bomb_hit = false;                                                                    // (no worker sets it off on purpose)
            Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(p), 4, 4);
            rig.run(900);
            const StandardBot& bot = rig.as<StandardBot>();
            if (route) {
                ASSERT_FALSE(sim.grid().has_bomb_at(TileCoord{20, 20}));
                ASSERT_EQ(bot.bombs().routed(), 1u);
                ASSERT_EQ(sim.get_unit(bomber).hp, 10);
            } else {
                ASSERT_EQ(bot.bombs().defused(), 0u);
            }
        }
    } TEST_END();

    TEST_CASE("AI51.7 A Pile Of The Class Hopeless (An Enemy's Hill Is Far Nearer) Is Left Alone While Another Pile Has Food; With nohope=0 The Ants That The Near Pile Has No Room For Go There") {
        size_t sent[2] = {0, 0};
        for (const bool skip : {true, false}) {
            sim::SimulationEngine sim;
            empty_field(sim, 79);
            add_pile(sim, 12, 12, 200, 31);
            add_pile(sim, 44, 10, 60, 100);                                                       // beside the hill of seat 1 at (50, 4)
            for (int i = 0; i < 14; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{8 + (i % 4), 9 + i / 4});
            for (uint8_t t = 1; t < 4; ++t) sim.spawn_unit(t, sim::AntType::Worker, TileCoord{kFightHills[t].x + 2, kFightHills[t].y + 6});
            LevelPlan p = peace_plan(Level::Medium);
            p.skip_hopeless = skip;
            p.bomb_detour = 0;
            Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(p), 4, 4);
            rig.run(120);
            const std::vector<int> piles = harvest_targets(rig, sim, 0, 2);
            ASSERT_FALSE(piles.empty());
            for (const auto& e : rig.proposed) {
                if (e.second.type == CommandType::GroupMove && e.second.tile_x > 38) sent[skip ? 0 : 1] += e.second.ants.size();
            }
        }
        ASSERT_EQ(sent[0], size_t{0});
        ASSERT_TRUE(sent[1] > 0u);
    } TEST_END();

    TEST_CASE("AI51.12 The Hopeless Rule Applies Only While Another Pile Has Food: When Every Pile Is Hopeless (Only The Pile Beside An Enemy's Hill) The Ants Still Go There With The Rule On") {
        size_t sent = 0;
        sim::SimulationEngine sim;
        empty_field(sim, 79);
        add_pile(sim, 44, 10, 60, 100);                                                           // beside the hill of seat 1 at (50, 4): the only pile
        for (int i = 0; i < 14; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{8 + (i % 4), 9 + i / 4});
        for (uint8_t t = 1; t < 4; ++t) sim.spawn_unit(t, sim::AntType::Worker, TileCoord{kFightHills[t].x + 2, kFightHills[t].y + 6});
        LevelPlan p = peace_plan(Level::Medium);
        ASSERT_TRUE(p.skip_hopeless);
        p.bomb_detour = 0;
        Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(p), 4, 4);
        rig.run(120);
        for (const auto& e : rig.proposed) {
            if (e.second.type == CommandType::GroupMove && e.second.tile_x > 38) sent += e.second.ants.size();
        }
        ASSERT_TRUE(sent > 0u);
    } TEST_END();

    TEST_CASE("AI51.8 The Plans And The Tune Keys: Easy Plays None Of It, Medium And Hard Play The Detour (1 And 2), The Bomber's Route And The Hopeless Rule; det, detpct, broute And nohope Put Them Over") {
        ASSERT_EQ(plan_for(Level::Easy).bomb_detour, uint8_t{0});
        ASSERT_EQ(plan_for(Level::Medium).bomb_detour, uint8_t{1});
        ASSERT_EQ(plan_for(Level::Hard).bomb_detour, uint8_t{2});
        ASSERT_FALSE(plan_for(Level::Easy).bomb_route);
        ASSERT_FALSE(plan_for(Level::Easy).skip_hopeless);
        for (const Level level : {Level::Medium, Level::Hard}) {
            ASSERT_TRUE(plan_for(level).bomb_route);
            ASSERT_TRUE(plan_for(level).skip_hopeless);
        }
        LevelPlan p = plan_for(Level::Hard);
        std::string err;
        ASSERT_TRUE(apply_tune(p, "det", 0, err) && p.bomb_detour == 0);
        ASSERT_TRUE(apply_tune(p, "det", 1, err) && p.bomb_detour == 1);
        ASSERT_TRUE(apply_tune(p, "detpct", 25, err) && p.detour_extra_percent == 25u);
        ASSERT_TRUE(apply_tune(p, "broute", 0, err) && !p.bomb_route);
        ASSERT_TRUE(apply_tune(p, "nohope", 0, err) && !p.skip_hopeless);
        ASSERT_TRUE(apply_tune(p, "nohope", 1, err) && p.skip_hopeless);
    } TEST_END();

    TEST_CASE("AI51.9 Whole Matches On TREASURE: Four Hard Standard Bots, Seeds 1 To 6, With The Detour And The Bomber's Route And Without Them (det=0, broute=0, The Bot As It Was): Fewer Bombs Walked Into, And The Score Does Not Fall") {
        uint64_t blasts[2] = {0, 0};
        int64_t score[2] = {0, 0};
        uint64_t orders[2] = {0, 0};
        for (const bool on : {true, false}) {
            for (uint32_t seed = 1; seed <= 6; ++seed) {
                BlastCount count;
                ArenaSpec spec;
                spec.level = &level_of("TREASURE");
                spec.seed = seed;
                for (uint8_t seat = 0; seat < 4; ++seat) {
                    BotSpec b;
                    b.seat = seat;
                    b.kind = "standard";
                    b.level = Level::Hard;
                    spec.bots.push_back(b);
                }
                spec.factory = [on](const BotSpec& b) {
                    return std::make_unique<StandardBot>(b.level, b.style, [on](LevelPlan& p) {
                        if (!on) {
                            p.bomb_detour = 0;
                            p.bomb_route = false;
                        }
                    });
                };
                spec.on_tick = [&count](const sim::SimulationEngine& sim) { count.scan(sim); };
                const ArenaResult r = play_match(spec);
                ASSERT_TRUE(r.error.empty());
                ASSERT_TRUE(r.match_over);
                blasts[on ? 0 : 1] += count.blasts;
                for (const ArenaSeatResult& seat : r.seats) {
                    score[on ? 0 : 1] += seat.score;
                    orders[on ? 0 : 1] += seat.orders;
                }
            }
        }
        ASSERT_TRUE(orders[0] >= 3000 && orders[1] >= 3000);                                  // the matches were played
        ASSERT_TRUE(blasts[1] >= 20);                                                         // the bot as it was walks into plenty of them
        ASSERT_TRUE(blasts[0] < blasts[1]);
        ASSERT_TRUE(score[0] * 100 >= score[1] * 97);                                         // (the same score within 3 percent)
    } TEST_END();
}
