// AI29: the field breach (docs/BOT_GATES.md, "The field breach"): one ant opens a pile that lies beyond a field of bombs, a fire wall and water (plan_field, next_field_step, FieldTask).
// The worlds are made by hand and drawn for every seat, straight and transposed (Drawing, b41_helpers.hpp); POPcOrN, a map of the library, is the one the rule was found on.
//
//   AI29.1   the plan of the field world: the route, the work in its order (bombs, bridge, fire), the sources on the pile's island, the bombs round the pile; no plan where the pile is free or
//            where a wall of one kind stands alone (the gate goals' own)
//   AI29.2   a Bomber on the hill's side leaves it with a hop of ONE bomb onto bomb-free ground in the field; a hurt one heals first; without that ground the hop is a chain
//   AI29.3   a Bomber in the field digs: it defuses a side of its own tile (N, S, W or E) and steps one tile at a time while bombs lie next to its way; over safe ground it walks
//   AI29.4   a Swimmer bridges the fire way's water from the land beside it; a Fire Ant from the island puts the route's fire cell out from a side
//   AI29.5   the Fire Ant's way leaves its power-up over water where the power-up that the ant leaves behind (on a free land tile beside it, the engine draws which) would shut a way
//            over land
//   AI29.6   the bombs of the seat's own color that lay on the map at the start (many levels give the map's bombs to team 0) are work like any other; one planted since is a wall
//   AI29.7   who plays it: Medium and Hard on a field world, not Easy, not with the switch off, not without a source of the Fire Ant, not when no ant of the bot can get the first work's
//            kind (the gate goals play then); the piles of the breach are decided at the first look, and the gate goals leave them to it
//   AI29.8   the breach in the engine: a Hard bot alone on the field world banks food, every seat, straight and transposed; and with the field's bombs in the seat's own color
//   AI29.9   POPcOrN: every seat alone banks food at Hard, and the gate goals leave the pile to the breach
//   AI29.10  an ant whose order the world does not carry out is given up and rests; after four ants given up the task waits until the bot's ants or the power-ups change
//   AI29.11  the carriers: a Swimmer of the ferry whose loop ended away from the pile is sent again
//   AI29.12  an ant that took a power-up never takes one at the end of a corridor (the power-up it leaves would shut it in); an ant of the level's kind does
//   AI29.13  the breach keeps the piles of its first look when the work changes their gates; in the stall detector's fallback it gives its ant back and orders nothing
//   AI29.14  while the breach is live the counters leave the map's own bombs alone, also once the gate goals are exhausted
#include <algorithm>
#include <cstdlib>
#include <string>

#include "ai_test.hpp"
#include "b41_helpers.hpp"
#include "ants_ai/field_plan.hpp"
#include "ants_ai/field_task.hpp"
#include "ants_ai/gate_goal.hpp"
#include "ants_ai/gate_info.hpp"
#include "ants_ai/gate_task.hpp"
#include "ants_sim/movement_tables.hpp"
#include "ants_sim/path_planner.hpp"

using namespace ai_test;
using namespace ants;
using namespace ants::ai;
using namespace b41;

namespace {

// The field world of seat 0 (its hill at (4, 4)), drawn for a seat: a fire wall across row 12 (x 0 .. 30, rock beyond to the field's edge), a field of map bombs below it (rows 13 .. 33,
// x 0 .. 30, rock beyond), with bomb-free ground at (20, 14) that a hop of one bomb from the hill's side reaches. In the field a lake ring (rows and columns 21 .. 26 of x 12 .. 17) round an
// island of 4 x 4 (x 13 .. 16, y 22 .. 25) that holds the pile (a crackers pile on (14 .. 15, 23 .. 24)), a Swimmer power-up (13, 25) and a Fire power-up (16, 25) on its south corners, bombs
// on the rest of its ring but the north-west corner (13, 22) and the two tiles between the power-ups. A Bomber power-up at home (12, 8) and a worker (8, 10). (A power-up placed by hand is
// solid, as one an ant leaves: no walk passes it and no flight lands on it. The power-ups of a map file need not be: POPcOrN, AI29.9.)
struct FieldWorld {
    struct Options {
        bool fire{true};             // the fire wall
        bool bombs{true};            // the field's bombs
        bool moat{true};             // the lake ring
        bool pocket{true};           // the bomb-free ground at (20, 14)
        bool fire_power_up{true};    // the island's Fire power-up
        bool west_fire{false};       // a second Fire power-up on an islet of its own in the field's west (x 2 .. 4, y 17 .. 19 in a ring of water), nearer the fire wall
        bool bomber_power_up{true};  // the Bomber power-up at home
        bool dead_end{false};        // a Bomber power-up at the end of a corridor of rock on the hill's side: (21 .. 23, 3), its way in from (20, 3), the power-up on (23, 3)
        bool vault{false};           // a second pile on the hill's side: a crackers pile on (21 .. 22, 4 .. 5) in a ring of rock (x 20 .. 23, y 3 .. 6) whose way in, (21, 6) and
                                     // (21, 7), holds two bombs of another team (a wall of one kind: the gate goals' own)
        uint8_t owner{255};          // the bombs' owner
    };
    sim::SimulationEngine sim;
    Drawing w;
    FieldWorld(Drawing d, const Options& o, uint32_t seed = 3) : w(d) {
        empty_field(sim, seed, 16800);
        for (int x = -2; x <= 59; ++x) {                                            // (x -2 and -1 are the columns beyond x 0 that a mirrored drawing has: they are drawn as x 0)
            const TileCoord c = w.at(x, 12);
            if (!sim.grid().in_bounds(c)) continue;
            if (x > 30) sim.set_terrain(c.x, c.y, sim::TERRAIN_OBSTACLE);
            else if (o.fire) sim.set_fire_at(c, 1000000);
        }
        for (int y = 13; y <= 33; ++y) {
            for (int x = -2; x <= 59; ++x) {
                const TileCoord c = w.at(x, y);
                if (!sim.grid().in_bounds(c)) continue;
                if (x > 30) {
                    sim.set_terrain(c.x, c.y, sim::TERRAIN_OBSTACLE);
                } else if (moat(x, y)) {
                    if (o.moat) sim.set_terrain(c.x, c.y, sim::TERRAIN_WATER);
                } else if (o.west_fire && islet(x, y) != 0) {
                    if (islet(x, y) == 1) sim.set_terrain(c.x, c.y, sim::TERRAIN_WATER);
                } else if (!pile_cell(x, y) && !bomb_free(x, y) && !(o.pocket && x == 20 && y == 14) && o.bombs) {
                    sim.grid_mut().place_bomb(static_cast<uint32_t>(c.x), static_cast<uint32_t>(c.y), o.owner);
                }
            }
        }
        place(13, 25, sim::AntType::Swimmer);
        if (o.fire_power_up) place(16, 25, sim::AntType::Fire);
        if (o.west_fire) place(3, 18, sim::AntType::Fire);
        if (o.bomber_power_up) place(12, 8, sim::AntType::Bomber);
        TileCoord anchor{-1, -1};                                                   // (a crackers pile covers the 2 x 2 up and left of its anchor: the anchor of a drawing is its largest corner)
        for (const TileCoord cell : {TileCoord{14, 23}, TileCoord{15, 23}, TileCoord{14, 24}, TileCoord{15, 24}}) {
            const TileCoord c = w.at(cell.x, cell.y);
            anchor = TileCoord{std::max(anchor.x, c.x), std::max(anchor.y, c.y)};
        }
        place_crackers(sim, anchor.x, anchor.y, 25);
        if (o.vault) {
            const uint8_t other = static_cast<uint8_t>((w.seat + 1) % 4);
            for (int y = 3; y <= 7; ++y) {
                for (int x = 20; x <= 23; ++x) {
                    const TileCoord c = w.at(x, y);
                    if (x == 21 && (y == 6 || y == 7)) sim.grid_mut().place_bomb(static_cast<uint32_t>(c.x), static_cast<uint32_t>(c.y), other);
                    else if ((y == 7 && (x == 20 || x == 22)) || (y <= 6 && (x == 20 || x == 23 || y == 3 || y == 6))) sim.set_terrain(c.x, c.y, sim::TERRAIN_OBSTACLE);
                }
            }
            TileCoord vault_anchor{-1, -1};
            for (const TileCoord cell : {TileCoord{21, 4}, TileCoord{22, 4}, TileCoord{21, 5}, TileCoord{22, 5}}) {
                const TileCoord c = w.at(cell.x, cell.y);
                vault_anchor = TileCoord{std::max(vault_anchor.x, c.x), std::max(vault_anchor.y, c.y)};
            }
            place_crackers(sim, vault_anchor.x, vault_anchor.y, 25);
        }
        if (o.dead_end) {
            for (int y = 2; y <= 4; ++y) {
                for (int x = 20; x <= 24; ++x) {
                    const TileCoord c = w.at(x, y);
                    if (!(y == 3 && x >= 20 && x <= 23)) sim.set_terrain(c.x, c.y, sim::TERRAIN_OBSTACLE);
                }
            }
            place(23, 3, sim::AntType::Bomber);
        }
        sim.spawn_unit(w.seat, sim::AntType::Worker, w.at(8, 10));
    }
    static bool moat(int x, int y) { return ((y == 21 || y == 26) && x >= 12 && x <= 17) || ((x == 12 || x == 17) && y >= 21 && y <= 26); }
    static bool pile_cell(int x, int y) { return (x == 14 || x == 15) && (y == 23 || y == 24); }
    static int islet(int x, int y) {                                                // 1: the islet's ring of water, 2: the islet (bomb-free), 0: neither
        if (x < 1 || x > 5 || y < 16 || y > 20) return 0;
        return (x == 1 || x == 5 || y == 16 || y == 20) ? 1 : 2;
    }
    static bool bomb_free(int x, int y) { return (x == 13 && y == 22) || (y == 25 && x >= 13 && x <= 16); }   // (the power-ups' tiles and the island's free ground)
    void place(int x, int y, sim::AntType kind) {
        const TileCoord c = w.at(x, y);
        sim.grid_mut().place_powerup(c.x, c.y, static_cast<uint8_t>(kind));
    }
    FieldPlan plan(const FieldHints& hints = FieldHints{}) {
        const MapInfo map(sim);
        const BotView view = BotView::build(sim, w.seat, &map);
        return plan_field(view, map, w.seat, hints);
    }
    // What the task keeps of a plan (FieldTask::step): the pile, the route, the sources and the ways
    static FieldHints kept_of(const FieldPlan& p) {
        FieldHints h;
        h.pile = p.pile;
        h.route = p.route;
        h.fire_source = p.fire_source;
        h.swim_source = p.swim_source;
        h.fire_way = p.fire_way;
        h.swim_way = p.swim_way;
        return h;
    }
    void clear_bombs(const std::vector<TileCoord>& tiles) {
        for (const TileCoord c : tiles) sim.grid_mut().clear_bomb(static_cast<uint32_t>(c.x), static_cast<uint32_t>(c.y));
    }
    // The step of an own ant of `type` put on `at` (seat 0's coordinates), for the plan of the world as it is (with what the task kept from before: `kept`)
    FieldStep step_of(sim::AntType type, TileCoord at, uint8_t hp = 10, const FieldHints& kept = FieldHints{}) {
        const uint32_t id = sim.spawn_unit(w.seat, type, w.at(at.x, at.y));
        const MapInfo map(sim);
        BotView view = BotView::build(sim, w.seat, &map);
        FieldHints hints = kept;
        hints.ant = id;
        const FieldPlan p = plan_field(view, map, w.seat, hints);
        for (const AntView& a : view.mine()) {
            if (a.id != id) continue;
            AntView ant = a;
            ant.hp = hp;
            return next_field_step(p, view, map, w.seat, ant, hints);
        }
        return FieldStep{};
    }
};

bool contains(const std::vector<TileCoord>& v, TileCoord c) { return std::find(v.begin(), v.end(), c) != v.end(); }

bool in_row(const Drawing& w, TileCoord c, int row, int x0, int x1) {                  // `c` is a tile of row `row` (x0 .. x1) of seat 0's world as `w` draws it
    for (int x = x0; x <= x1; ++x) {
        if (w.at(x, row) == c) return true;
    }
    return false;
}

bool is_moat(const Drawing& w, TileCoord c) {                                          // `c` is a tile of the lake ring as `w` draws it
    for (int y = 21; y <= 26; ++y) {
        for (int x = 12; x <= 17; ++x) {
            if (FieldWorld::moat(x, y) && w.at(x, y) == c) return true;
        }
    }
    return false;
}

int side_distance(TileCoord a, TileCoord b) { return std::abs(a.x - b.x) + std::abs(a.y - b.y); }

TileCoord seat0_of(const Drawing& w, TileCoord c) {                                         // the tile of seat 0's world that `w` draws on `c`
    int x = (w.seat == 1 || w.seat == 3) ? 57 - c.x : c.x;
    int y = (w.seat == 2 || w.seat == 3) ? 57 - c.y : c.y;
    if (w.transposed) std::swap(x, y);
    return TileCoord{x, y};
}

LevelPlan field_plan_of(Level level = Level::Hard) { return peace_plan(level); }

// The walk the engine gives an ant of `seat` (a Swimmer where `swimmer`) from `from` to `to`: its own A* (sim::PathSearch) on its step costs, where another team's bomb is no obstacle
// (an ant that steps on one is thrown) and the seat's own is a wall
std::vector<TileCoord> engine_walk(const sim::SimulationEngine& sim, uint8_t seat, bool swimmer, TileCoord from, TileCoord to) {
    const sim::Grid& grid = sim.grid();
    const auto cost = [&](TileCoord a, TileCoord b) -> uint32_t {
        constexpr uint32_t kBlocked = static_cast<uint32_t>(MapInfo::kPathCostLimit);
        if (!grid.in_bounds(b)) return kBlocked;
        const bool water = grid.terrain_class_at(b) == sim::movement::kTerrainWater;
        if (!(b == to || (swimmer && water && !grid.is_solid_object(b)) || MapInfo::walkable(grid, seat, b))) return kBlocked;
        uint32_t step = sim::movement::terrain_step_weight(grid.terrain_class_at(a), swimmer) + sim::movement::terrain_step_weight(grid.terrain_class_at(b), swimmer);
        if (a.x != b.x && a.y != b.y) step = static_cast<uint32_t>(static_cast<double>(step) * 1.4);
        return std::min(step >> 1, kBlocked);
    };
    sim::PathGridPool pool;
    sim::PathSearch search(0, from, to, static_cast<uint16_t>(grid.height()), static_cast<uint16_t>(grid.width()), pool);
    for (int slices = 0; slices < 64 && !search.finished(); ++slices) {
        if (search.step(sim::PathSearch::kDefaultBudget, cost) == 0) break;
    }
    return search.finished() ? search.path() : std::vector<TileCoord>{};
}

// The bombs on the walks of a Swimmer carrier between the hill of `seat` and the pile: from each tile it leaves the hill by to each cell of the pile, and back to the hill
int bombs_on_carrier_walks(const sim::SimulationEngine& sim, uint8_t seat) {
    const MapInfo map(sim);
    int bombs = 0;
    for (const PileInfo& pile : map.piles()) {
        for (const TileCoord cell : pile.cells) {
            for (const TileCoord start : map.hill(seat).starts) {
                const std::vector<TileCoord> out = engine_walk(sim, seat, true, start, cell);
                for (const TileCoord c : out) bombs += sim.grid().has_bomb_at(c) ? 1 : 0;
                if (out.size() < 2) continue;
                for (const TileCoord c : engine_walk(sim, seat, true, out[out.size() - 2], map.hill(seat).entrance)) bombs += sim.grid().has_bomb_at(c) ? 1 : 0;
            }
        }
    }
    return bombs;
}

}  // namespace

void run_field_tests() {
    TEST_CASE("AI29.1 The Plan Of The Field World: The Route Through The Fire Wall And The Field To The Pile's Island, The Work In Its Order (Bombs, Then The Bridge, Then The Fire), The Sources On The Pile's Island, The Bombs Round The Pile; No Plan Where The Pile Is Free Or A Wall Of One Kind Stands Alone (Every Seat, Straight And Transposed)") {
        for (uint8_t d = 0; d < 8; ++d) {
            const Drawing w{static_cast<uint8_t>(d >> 1), (d & 1) != 0};
            FieldWorld f(w, FieldWorld::Options{});
            const FieldPlan p = f.plan();
            ASSERT_TRUE(p.field);
            ASSERT_TRUE(p.feasible());
            ASSERT_TRUE(p.swim);
            ASSERT_EQ(p.work(), FieldWork::Bombs);
            ASSERT_EQ(p.fires.size(), 1u);
            ASSERT_TRUE(in_row(w, p.fires[0], 12, 0, 30));                              // a cell of the fire wall
            ASSERT_EQ(p.fire_source, w.at(16, 25));                                   // the island's Fire power-up: the Bomber goes to the island anyway (a source elsewhere costs a hop more)
            ASSERT_EQ(p.swim_source, w.at(13, 25));
            ASSERT_FALSE(p.waters.empty());                                             // the Fire Ant leaves the island over bridges: the lake ring's water
            for (const TileCoord c : p.waters) ASSERT_TRUE(is_moat(w, c));
            ASSERT_TRUE(p.fire_way.size() >= 2);
            ASSERT_EQ(p.fire_way.front(), p.fire_source);
            ASSERT_EQ(side_distance(p.fire_way.back(), p.fires[0]), 1);                    // it puts the fire out from a side (N, S, W or E)
            ASSERT_TRUE(p.swim_way.size() >= 2);
            ASSERT_EQ(p.swim_way.front(), p.swim_source);
            bool beside_water = false;
            for (const TileCoord c : p.waters) beside_water = beside_water || side_distance(p.swim_way.back(), c) == 1;
            ASSERT_TRUE(beside_water);
            ASSERT_FALSE(p.bombs.empty());
            for (const TileCoord around : {TileCoord{14, 22}, TileCoord{15, 22}, TileCoord{16, 22}, TileCoord{13, 23}, TileCoord{16, 23}, TileCoord{13, 24}, TileCoord{16, 24}}) {
                ASSERT_TRUE(contains(p.bombs, w.at(around.x, around.y)));             // the bombs round the pile: a carrier comes at it from any side
            }
            ASSERT_TRUE(p.route.size() >= 2);
            const TileCoord end = p.route.back();
            bool next_to_pile = false;
            for (const TileCoord cell : {TileCoord{14, 23}, TileCoord{15, 23}, TileCoord{14, 24}, TileCoord{15, 24}}) next_to_pile = next_to_pile || end.chebyshev_dist(w.at(cell.x, cell.y)) == 1;
            ASSERT_TRUE(next_to_pile);
        }
        for (uint8_t d = 0; d < 8; ++d) {                                               // a Fire power-up nearer the wall, on an islet of its own: its way costs the one ant a hop more
            const Drawing w{static_cast<uint8_t>(d >> 1), (d & 1) != 0};
            FieldWorld::Options o;
            o.west_fire = true;
            FieldWorld f(w, o);
            ASSERT_EQ(f.plan().fire_source, w.at(16, 25));
        }
        for (uint8_t d = 0; d < 8; ++d) {                                               // the same pile with nothing in its way: no plan
            const Drawing w{static_cast<uint8_t>(d >> 1), (d & 1) != 0};
            FieldWorld::Options o;
            o.fire = false;
            o.bombs = false;
            o.moat = false;
            FieldWorld f(w, o);
            ASSERT_FALSE(f.plan().field);
        }
        for (uint8_t d = 0; d < 8; ++d) {                                               // a fire wall alone: the gate goals' own (a Fire Ant puts it out from home)
            const Drawing w{static_cast<uint8_t>(d >> 1), (d & 1) != 0};
            FieldWorld::Options o;
            o.bombs = false;
            o.moat = false;
            FieldWorld f(w, o);
            ASSERT_FALSE(f.plan().field);
        }
    } TEST_END();

    TEST_CASE("AI29.2 A Bomber On The Hill's Side Leaves It With A Hop Of One Bomb Onto Bomb-Free Ground In The Field; A Hurt One Heals First; Without That Ground The Hop Is A Chain (Every Seat, Straight And Transposed)") {
        for (uint8_t d = 0; d < 8; ++d) {
            const Drawing w{static_cast<uint8_t>(d >> 1), (d & 1) != 0};
            {
                FieldWorld f(w, FieldWorld::Options{});
                const FieldStep st = f.step_of(sim::AntType::Bomber, TileCoord{16, 11}, 10);
                ASSERT_EQ(st.work, FieldWork::Bombs);
                ASSERT_EQ(st.hop.bombs, 1);
                ASSERT_EQ(st.hop.land(), w.at(20, 14));                                    // the pocket
                ASSERT_TRUE(st.action == FieldAction::Walk || st.action == FieldAction::Step || st.action == FieldAction::Plant || st.action == FieldAction::Defuse);
            }
            {
                FieldWorld f(w, FieldWorld::Options{});
                const FieldStep st = f.step_of(sim::AntType::Bomber, TileCoord{16, 11}, 6);   // two hit points short of a full ant's eight for the field
                ASSERT_EQ(st.action, FieldAction::Heal);
            }
            {
                FieldWorld::Options o;
                o.pocket = false;
                FieldWorld f(w, o);
                const FieldStep st = f.step_of(sim::AntType::Bomber, TileCoord{16, 11}, 10);
                ASSERT_TRUE(st.hop.bombs >= 2);                                              // over the map's bombs
            }
        }
    } TEST_END();

    TEST_CASE("AI29.3 A Bomber In The Field Digs: It Defuses A Side Of Its Own Tile And Steps One Tile At A Time While Bombs Lie Next To Its Way; Over Safe Ground It Walks (Every Seat, Straight And Transposed)") {
        for (uint8_t d = 0; d < 8; ++d) {
            const Drawing w{static_cast<uint8_t>(d >> 1), (d & 1) != 0};
            {
                FieldWorld f(w, FieldWorld::Options{});
                const FieldStep st = f.step_of(sim::AntType::Bomber, TileCoord{20, 14}, 8);
                ASSERT_EQ(st.action, FieldAction::Defuse);
                ASSERT_EQ(side_distance(st.tile, w.at(20, 14)), 1);                         // N, S, W or E: the engine works an ability from a side
            }
            {
                FieldWorld f(w, FieldWorld::Options{});
                f.clear_bombs({w.at(19, 14), w.at(18, 14)});
                const FieldStep st = f.step_of(sim::AntType::Bomber, TileCoord{20, 14}, 8);
                ASSERT_EQ(st.action, FieldAction::Step);
                ASSERT_EQ(st.tile, w.at(19, 14));                                          // one tile: the engine's walk to a tile further on may cut over a bomb
            }
            {
                FieldWorld f(w, FieldWorld::Options{});
                const FieldStep st = f.step_of(sim::AntType::Bomber, TileCoord{12, 8}, 10);
                ASSERT_EQ(st.action, FieldAction::Walk);
                ASSERT_TRUE(st.tile.chebyshev_dist(w.at(12, 8)) >= 2);
            }
            {   // a bomb of the work on a diagonal of the ant, its sides clear: a step to a side first (an ability works from a side; the engine's walk to another side may cross a bomb)
                FieldWorld f(w, FieldWorld::Options{});
                const FieldPlan p = f.plan();
                TileCoord b{-1, -1};
                for (const TileCoord c : p.route) {
                    if (b.x < 0 && f.sim.grid().has_bomb_at(c)) b = c;
                }
                ASSERT_TRUE(b.x >= 0);
                TileCoord a{-1, -1};
                for (const TileCoord c : {TileCoord{b.x + 1, b.y + 1}, TileCoord{b.x - 1, b.y + 1}, TileCoord{b.x + 1, b.y - 1}, TileCoord{b.x - 1, b.y - 1}}) {
                    if (a.x < 0 && f.sim.grid().has_bomb_at(c)) a = c;
                }
                ASSERT_TRUE(a.x >= 0);
                f.clear_bombs({a, TileCoord{a.x + 1, a.y}, TileCoord{a.x - 1, a.y}, TileCoord{a.x, a.y + 1}, TileCoord{a.x, a.y - 1}});
                const FieldStep st = f.step_of(sim::AntType::Bomber, seat0_of(w, a), 8);
                ASSERT_EQ(st.action, FieldAction::Step);
                ASSERT_EQ(side_distance(st.tile, a), 1);
            }
        }
    } TEST_END();

    TEST_CASE("AI29.4 A Swimmer Bridges The Fire Way's Water From The Land Beside It; A Fire Ant From The Island Puts The Route's Fire Cell Out From A Side (Every Seat, Straight And Transposed)") {
        for (uint8_t d = 0; d < 8; ++d) {
            const Drawing w{static_cast<uint8_t>(d >> 1), (d & 1) != 0};
            FieldWorld f(w, FieldWorld::Options{});
            const FieldPlan first = f.plan();
            const FieldHints kept = FieldWorld::kept_of(first);
            f.clear_bombs(first.bombs);                                                    // the Bomber's work done
            const FieldPlan p = f.plan(kept);
            ASSERT_EQ(p.work(), FieldWork::Bridges);
            ASSERT_FALSE(p.waters.empty());
            TileCoord beside{-1, -1};                                                       // the land beside the first water: no water, bomb, power-up or food on it
            for (const TileCoord c : {TileCoord{p.waters[0].x, p.waters[0].y - 1}, TileCoord{p.waters[0].x, p.waters[0].y + 1}, TileCoord{p.waters[0].x - 1, p.waters[0].y},
                                      TileCoord{p.waters[0].x + 1, p.waters[0].y}}) {
                const sim::Grid& g = f.sim.grid();
                if (beside.x < 0 && g.in_bounds(c) && g.terrain_class_at(c) != sim::movement::kTerrainWater && !g.has_bomb_at(c) && !g.is_solid_object(c)) beside = c;
            }
            ASSERT_TRUE(beside.x >= 0);
            {
                FieldWorld g(w, FieldWorld::Options{});
                g.clear_bombs(first.bombs);
                const FieldStep st = g.step_of(sim::AntType::Swimmer, seat0_of(w, beside), 6, kept);
                ASSERT_EQ(st.action, FieldAction::Bridge);
                ASSERT_TRUE(contains(p.waters, st.tile));
                ASSERT_EQ(side_distance(st.tile, beside), 1);
            }
            for (const TileCoord c : p.waters) f.sim.grid_mut().set_bridge_at(c, 4, 3600);   // the bridges built
            const FieldPlan q = f.plan(kept);
            ASSERT_EQ(q.work(), FieldWork::Fire);
            ASSERT_EQ(q.fire_way.front(), w.at(16, 25));                                   // from the island's Fire power-up, through the field (it walks through the wall where that is shorter)
            const TileCoord side = q.fire_way.back();
            ASSERT_EQ(side_distance(side, q.fires[0]), 1);
            const FieldStep st = f.step_of(sim::AntType::Fire, seat0_of(w, side), 6, kept);
            ASSERT_EQ(st.action, FieldAction::Extinguish);
            ASSERT_EQ(st.tile, q.fires[0]);
        }
    } TEST_END();

    TEST_CASE("AI29.5 The Fire Ant's Way Leaves Its Power-Up Over Water Where The Power-Up That The Ant Leaves Behind Would Shut A Way Over Land (Every Seat, Straight And Transposed)") {
        for (uint8_t d = 0; d < 8; ++d) {
            const Drawing w{static_cast<uint8_t>(d >> 1), (d & 1) != 0};
            FieldWorld::Options o;                                                         // the field's bombs the seat's own: the island's ring keeps its bombs (they never throw a carrier),
            o.owner = w.seat;                                                              // so the Fire power-up has one free land tile beside it, (15, 25): the power-up the ant leaves lands there
            FieldWorld f(w, o);
            const FieldPlan p = f.plan();
            ASSERT_TRUE(p.fire_way.size() >= 2);
            ASSERT_EQ(p.fire_way[0], w.at(16, 25));
            ASSERT_TRUE(is_moat(w, p.fire_way[1]));
        }
    } TEST_END();

    TEST_CASE("AI29.6 The Bombs Of The Seat's Own Color That Lay On The Map At The Start Are Work Like Any Other; One Planted Since Is A Wall (Every Seat, Straight And Transposed)") {
        for (uint8_t d = 0; d < 8; ++d) {
            const Drawing w{static_cast<uint8_t>(d >> 1), (d & 1) != 0};
            FieldWorld::Options o;
            o.owner = w.seat;
            FieldWorld f(w, o);
            const FieldPlan unknown = f.plan();                                              // before the task's first look every bomb of the seat counts as the map's own
            ASSERT_TRUE(unknown.field);
            ASSERT_TRUE(unknown.feasible());
            ASSERT_FALSE(unknown.bombs.empty());
            FieldHints none;
            none.map_bombs_known = true;                                                    // none of them lay on the map at the start: they are walls to the seat's walkers
            ASSERT_FALSE(f.plan(none).field);
            FieldHints start;
            start.map_bombs_known = true;
            const MapInfo map(f.sim);
            const BotView view = BotView::build(f.sim, w.seat, &map);
            for (const BombView& b : view.bombs()) start.map_bombs.push_back(b.tile);
            const FieldPlan known = f.plan(start);
            ASSERT_TRUE(known.field);
            ASSERT_EQ(known.bombs.size(), unknown.bombs.size());
            FieldWorld neutral(w, FieldWorld::Options{});                                    // the bombs of no team: a carrier that steps on one is thrown, so the ways round the dug route are work
            ASSERT_TRUE(known.bombs.size() < neutral.plan().bombs.size());                  // the seat's own never throw its carriers (its walkers do not step on them)
        }
    } TEST_END();

    TEST_CASE("AI29.7 Who Plays It: Medium And Hard On A Field World, Not Easy, Not With The Switch Off, Not Without A Source Of The Fire Ant, Not Without A Source Of The First Work's Kind (The Gate Goals Play Then); The Gate Goals Leave Its Piles To It (Every Seat, Straight And Transposed)") {
        for (uint8_t d = 0; d < 8; ++d) {
            const Drawing w{static_cast<uint8_t>(d >> 1), (d & 1) != 0};
            const auto decided = [&](Level level, bool field_switch, const FieldWorld::Options& o, bool& gate_map) {
                FieldWorld f(w, o);
                LevelPlan plan = field_plan_of(level);
                plan.gate_field = field_switch;
                Rig rig(f.sim, w.seat, level, std::make_unique<StandardBot>(plan));
                rig.run(40);
                gate_map = rig.as<StandardBot>().tactics().gate_map;
                return rig.as<StandardBot>().tactics().field_map;
            };
            bool gate = false;
            ASSERT_TRUE(decided(Level::Hard, true, FieldWorld::Options{}, gate));
            ASSERT_TRUE(gate);
            ASSERT_TRUE(decided(Level::Medium, true, FieldWorld::Options{}, gate));
            ASSERT_FALSE(decided(Level::Easy, true, FieldWorld::Options{}, gate));
            ASSERT_FALSE(gate);
            ASSERT_FALSE(decided(Level::Hard, false, FieldWorld::Options{}, gate));
            ASSERT_TRUE(gate);
            FieldWorld::Options no_fire;
            no_fire.fire_power_up = false;
            ASSERT_FALSE(decided(Level::Hard, true, no_fire, gate));
            ASSERT_TRUE(gate);
            FieldWorld::Options no_bomber;                                                   // no ant of the bot can get the first work's kind: the breach cannot start
            no_bomber.bomber_power_up = false;
            ASSERT_FALSE(decided(Level::Hard, true, no_bomber, gate));
            ASSERT_TRUE(gate);
            FieldWorld::Options mixed;                                                       // with a wall of one kind before a second pile: the piles of the breach are decided at the first look
            mixed.vault = true;
            FieldWorld f(w, mixed);
            const MapInfo map(f.sim);
            uint32_t vault = 0;
            for (const PileInfo& pile : map.piles()) {
                if (contains(pile.cells, w.at(21, 4))) vault = pile.index;
            }
            const uint32_t field_pile = f.plan().pile;
            ASSERT_TRUE(field_pile != vault);
            Rig rig(f.sim, w.seat, Level::Hard, std::make_unique<StandardBot>(field_plan_of()));
            rig.run(40);
            const std::vector<uint32_t> owned = rig.as<StandardBot>().tactics().field_piles;
            ASSERT_EQ(owned.size(), 1u);
            ASSERT_EQ(owned[0], field_pile);
            FieldHints kept;                                                                 // the breach plays those piles only (the vault's two bombs would be a cheaper route)
            kept.piles = owned;
            ASSERT_EQ(f.plan(kept).pile, field_pile);
            const BotView view = BotView::build(f.sim, w.seat, &map);
            const GateInfo gates = GateInfo::analyse(map, f.sim.grid(), w.seat);
            GoalRules rules;
            rules.field_piles = owned;                                                       // the gate goals leave that pile to the field breach: the vault's bombs are their goal
            const GateGoal goal = plan_gate_goal(gates, view, rules);
            ASSERT_EQ(goal.kind, GoalKind::Defuse);
            ASSERT_FALSE(goal.powerup);
            ASSERT_EQ(goal.target, vault);
            FieldWorld alone(w, FieldWorld::Options{});                                      // and with no other pile they have no goal at all
            const MapInfo amap(alone.sim);
            const BotView aview = BotView::build(alone.sim, w.seat, &amap);
            const GateInfo agates = GateInfo::analyse(amap, alone.sim.grid(), w.seat);
            ASSERT_EQ(plan_gate_goal(agates, aview, rules).kind, GoalKind::None);
            ASSERT_TRUE(plan_gate_goal(agates, aview, GoalRules{}).kind != GoalKind::None);
        }
    } TEST_END();

    TEST_CASE("AI29.8 The Breach In The Engine: A Hard Bot Alone On The Field World Banks Food, The Gate Goals Leave It Alone, And When It Is Done The Carriers' Walks Cross No Bomb (Every Seat, Straight And Transposed; The Field's Bombs Of No Team And Of The Seat)") {
        const auto breach = [](const Drawing& w, const FieldWorld::Options& o) {
            FieldWorld f(w, o);
            Rig rig(f.sim, w.seat, Level::Hard, std::make_unique<StandardBot>(field_plan_of()));
            auto& bot = rig.as<StandardBot>();
            uint64_t ticks = 0;
            while (ticks < 12000 && f.sim.get_player_score(w.seat) == 0) {
                rig.run(200);
                ticks += 200;
            }
            std::printf("      seat %u%s, bombs of %u: banked at %llu, %u field orders, %u ants given up\n", static_cast<unsigned>(w.seat), w.transposed ? " transposed" : "", static_cast<unsigned>(o.owner), static_cast<unsigned long long>(ticks), bot.field().orders(), bot.field().given_up());
            ASSERT_TRUE(bot.tactics().field_map);
            ASSERT_TRUE(f.sim.get_player_score(w.seat) > 0);
            ASSERT_TRUE(bot.field().done_tick() > 0);
            ASSERT_EQ(bot.gate_goals().hops_ordered() + bot.gate_goals().defuses() + bot.gate_goals().extinguishes(), 0u);
            ASSERT_EQ(bombs_on_carrier_walks(f.sim, w.seat), 0);
        };
        for (uint8_t d = 0; d < 8; ++d) {
            const Drawing w{static_cast<uint8_t>(d >> 1), (d & 1) != 0};
            breach(w, FieldWorld::Options{});
            FieldWorld::Options own;
            own.owner = w.seat;
            breach(w, own);
        }
    } TEST_END();

    TEST_CASE("AI29.9 POPcOrN: Every Seat Alone Banks Food At Hard, And The Gate Goals Leave The Pile To The Breach") {
        assets::LevelData level;
        ASSERT_TRUE(level.load_from_file(std::string(ANTS_SOURCE_DIR) + "/Community-Maps/POPcOrN.lvl"));
        for (uint8_t seat = 0; seat < 4; ++seat) {
            sim::SimulationEngine sim;
            sim.init(level, 1, static_cast<uint8_t>(1u << seat));
            Rig rig(sim, seat, Level::Hard, std::make_unique<StandardBot>(field_plan_of()));
            uint64_t ticks = 0;
            while (ticks < 14400 && sim.get_player_score(seat) == 0) {
                rig.run(200);
                ticks += 200;
            }
            std::printf("      seat %u: banked at %llu, field orders %u, given up %u\n", static_cast<unsigned>(seat), static_cast<unsigned long long>(ticks), rig.as<StandardBot>().field().orders(), rig.as<StandardBot>().field().given_up());
            ASSERT_TRUE(rig.as<StandardBot>().tactics().field_map);
            ASSERT_TRUE(sim.get_player_score(seat) > 0);
            const GateGoalTask& goals = rig.as<StandardBot>().gate_goals();
            ASSERT_EQ(goals.hops_ordered() + goals.defuses() + goals.extinguishes(), 0u);   // the gate goals leave the pile to the breach (the walk to the lake and its hops were theirs)
        }
    } TEST_END();

    TEST_CASE("AI29.10 An Ant Whose Order The World Does Not Carry Out Is Given Up And Rests; After Four Ants Given Up The Task Waits Until The Bot's Ants Or The Power-Ups Change (Every Seat, Straight And Transposed)") {
        for (uint8_t d = 0; d < 8; ++d) {
            const Drawing w{static_cast<uint8_t>(d >> 1), (d & 1) != 0};
            FieldWorld f(w, FieldWorld::Options{});
            for (int y = 8; y <= 11; ++y) {                                                  // a room of rock round the worker (8, 10): x 6 .. 10, y 8 .. 11, its door (10, 10)
                for (int x = 6; x <= 10; ++x) {
                    const TileCoord c = w.at(x, y);
                    if ((x == 6 || x == 10 || y == 8) && !(x == 10 && y == 10)) f.sim.set_terrain(c.x, c.y, sim::TERRAIN_OBSTACLE);
                }
            }
            f.sim.spawn_unit(static_cast<uint8_t>((w.seat + 1) % 4), sim::AntType::Worker, w.at(10, 10));   // an ant of another team stands in the door: the engine finds no walk out
            Rig rig(f.sim, w.seat, Level::Hard, std::make_unique<StandardBot>(field_plan_of()));
            auto& bot = rig.as<StandardBot>();
            rig.run(400);
            ASSERT_EQ(bot.field().given_up(), 1u);                                           // the same order four times and the ant unchanged
            const uint32_t first = bot.field().orders();
            ASSERT_TRUE(first <= 5u);
            rig.run(1800);
            ASSERT_EQ(bot.field().orders(), first);                                          // it rests (2,400 ticks), and the bot has no other ant
            uint64_t ticks = 2200;
            while (bot.field().live() && ticks < 20000) {
                rig.run(200);
                ticks += 200;
            }
            ASSERT_FALSE(bot.field().live());
            ASSERT_EQ(bot.field().given_up(), 4u);
            const uint32_t resting = bot.field().orders();
            rig.run(6000);
            ASSERT_EQ(bot.field().orders(), resting);                                        // it waits ...
            const TileCoord c = w.at(8, 9);
            f.sim.grid_mut().place_powerup(c.x, c.y, static_cast<uint8_t>(sim::AntType::Bomber));   // ... until a power-up appears
            rig.run(400);
            ASSERT_TRUE(bot.field().live());
            ASSERT_TRUE(bot.field().orders() > resting);
        }
    } TEST_END();

    TEST_CASE("AI29.11 The Field Breach's Carriers: A Swimmer Of The Ferry Whose Loop Ended Away From The Pile Is Sent Again (Every Seat, Straight And Transposed)") {
        for (uint8_t d = 0; d < 8; ++d) {
            const Drawing w{static_cast<uint8_t>(d >> 1), (d & 1) != 0};
            FieldWorld f(w, FieldWorld::Options{});
            Rig rig(f.sim, w.seat, Level::Hard, std::make_unique<StandardBot>(field_plan_of()));
            auto& bot = rig.as<StandardBot>();
            uint64_t ticks = 0;
            while (ticks < 12000 && f.sim.get_player_score(w.seat) == 0) {
                rig.run(200);
                ticks += 200;
            }
            ASSERT_TRUE(f.sim.get_player_score(w.seat) > 0);
            uint32_t carrier = 0;                                                            // the ferry's Swimmer, on its way back to the pile (empty)
            for (int k = 0; k < 400 && carrier == 0; ++k) {
                rig.run(5);
                const MapInfo map(f.sim);
                const BotView view = BotView::build(f.sim, w.seat, &map);
                for (const AntView& a : view.mine()) {
                    uint32_t pile = 0;
                    if (a.type == sim::AntType::Swimmer && !a.holding && bot.ferry().assigned_pile(a.id, pile)) carrier = a.id;
                }
            }
            ASSERT_TRUE(carrier != 0);
            const int32_t banked = f.sim.get_player_score(w.seat);
            f.sim.issue_group_move_order({carrier}, w.at(9, 9));                             // an order from outside the bot ends the engine's loop on the hill's side
            rig.run(1200);
            ASSERT_TRUE(bot.ferry().ended() >= 1u);
            rig.run(2400);
            ASSERT_TRUE(f.sim.get_player_score(w.seat) > banked);                           // it carries again
        }
    } TEST_END();

    TEST_CASE("AI29.12 An Ant That Took A Power-Up Never Takes One At The End Of A Corridor: The Power-Up It Leaves Would Shut It In; An Ant Of The Level's Kind (It Leaves Nothing) Takes It (Every Seat, Straight And Transposed)") {
        for (uint8_t d = 0; d < 8; ++d) {
            const Drawing w{static_cast<uint8_t>(d >> 1), (d & 1) != 0};
            FieldWorld::Options o;
            o.dead_end = true;
            {
                FieldWorld f(w, o);
                const FieldStep st = f.step_of(sim::AntType::Swimmer, TileCoord{19, 3}, 10);   // nearer the corridor's Bomber power-up than the one at (12, 8)
                ASSERT_EQ(st.work, FieldWork::Bombs);
                ASSERT_TRUE(st.tile.x >= 0);
                ASSERT_TRUE(st.tile.chebyshev_dist(w.at(12, 8)) < st.tile.chebyshev_dist(w.at(23, 3)));
            }
            {
                FieldWorld f(w, o);
                const FieldStep st = f.step_of(sim::AntType::Worker, TileCoord{19, 3}, 10);
                ASSERT_TRUE(st.tile.x >= 0);
                ASSERT_TRUE(st.tile.chebyshev_dist(w.at(23, 3)) < st.tile.chebyshev_dist(w.at(12, 8)));
            }
        }
    } TEST_END();

    TEST_CASE("AI29.13 The Breach Keeps The Piles Of Its First Look When The Work Changes Their Gates; In The Stall Detector's Fallback It Gives Its Ant Back And Orders Nothing (Every Seat, Straight And Transposed)") {
        for (uint8_t d = 0; d < 8; ++d) {
            const Drawing w{static_cast<uint8_t>(d >> 1), (d & 1) != 0};
            {
                FieldWorld f(w, FieldWorld::Options{});
                const TileCoord gate = f.plan().fires[0];                                    // the fire cell of the route that the breach plans first
                Rig rig(f.sim, w.seat, Level::Hard, std::make_unique<StandardBot>(field_plan_of()));
                rig.run(40);
                ASSERT_TRUE(rig.as<StandardBot>().tactics().field_map);
                for (int y = 21; y <= 26; ++y) {                                             // the lake ring bridged and the fire wall out: the pile is behind bombs alone now (a wall of
                    for (int x = 12; x <= 17; ++x) {                                         // one kind, which the gate goals would play on a new map), and it is still the breach's
                        if (FieldWorld::moat(x, y)) f.sim.grid_mut().set_bridge_at(w.at(x, y), 4, 1000000);
                    }
                }
                for (int x = -2; x <= 30; ++x) {
                    const TileCoord c = w.at(x, 12);
                    if (f.sim.grid().in_bounds(c) && f.sim.has_fire_at(c)) f.sim.grid_mut().clear_firewall(static_cast<uint32_t>(c.x), static_cast<uint32_t>(c.y));
                }
                f.sim.set_terrain(gate.x, gate.y, sim::TERRAIN_OBSTACLE);                    // and its route is shut: it plans a new one, to the same pile
                uint64_t ticks = 40;
                while (ticks < 12000 && f.sim.get_player_score(w.seat) == 0) {
                    rig.run(200);
                    ticks += 200;
                }
                ASSERT_TRUE(f.sim.get_player_score(w.seat) > 0);
            }
            {
                FieldWorld f(w, FieldWorld::Options{});                                       // the room of AI29.10: the breach ant's walk out is refused, its order is sent again and again
                for (int y = 8; y <= 11; ++y) {
                    for (int x = 6; x <= 10; ++x) {
                        const TileCoord c = w.at(x, y);
                        if ((x == 6 || x == 10 || y == 8) && !(x == 10 && y == 10)) f.sim.set_terrain(c.x, c.y, sim::TERRAIN_OBSTACLE);
                    }
                }
                f.sim.spawn_unit(static_cast<uint8_t>((w.seat + 1) % 4), sim::AntType::Worker, w.at(10, 10));
                LevelPlan plan = field_plan_of();
                plan.repeat_limit = 3;                                                        // the stall detector takes the third such order for a stall: the fallback, for longer than
                plan.fallback_ticks = 6000;                                                   // the rest of an ant given up
                Rig rig(f.sim, w.seat, Level::Hard, std::make_unique<StandardBot>(plan));
                auto& bot = rig.as<StandardBot>();
                bool held = false;
                for (int t = 0; t < 2000 && !bot.in_fallback(); t += 2) {
                    rig.run(2);
                    held = held || bot.ledger().count(StandardBot::kField) != 0u;
                }
                ASSERT_TRUE(bot.in_fallback());
                ASSERT_TRUE(held);
                ASSERT_EQ(bot.ledger().count(StandardBot::kField), 0u);
                const uint32_t orders = bot.field().orders();
                uint64_t in_fallback = 0;
                for (; in_fallback < 5000 && bot.in_fallback(); in_fallback += 50) {
                    rig.run(50);
                    ASSERT_EQ(bot.ledger().count(StandardBot::kField), 0u);
                }
                ASSERT_TRUE(in_fallback >= 3000u);                                            // (longer than the rest of 2,400 ticks)
                ASSERT_EQ(bot.field().orders(), orders);
            }
        }
    } TEST_END();

    TEST_CASE("AI29.14 While The Breach Is Live The Counters Leave The Map's Own Bombs Alone, Also Once The Gate Goals Are Exhausted (Every Seat, Straight And Transposed)") {
        for (uint8_t d = 0; d < 8; ++d) {
            const Drawing w{static_cast<uint8_t>(d >> 1), (d & 1) != 0};
            for (const bool field : {true, false}) {
                FieldWorld f(w, FieldWorld::Options{});
                const TileCoord bomb = w.at(2, 9);                                           // a map bomb of no team by the hill, off the breach's ways
                f.sim.grid_mut().place_bomb(static_cast<uint32_t>(bomb.x), static_cast<uint32_t>(bomb.y), 255);
                f.sim.spawn_unit(w.seat, sim::AntType::Bomber, w.at(3, 10));
                LevelPlan plan = field_plan_of();
                plan.gate_field = field;
                Rig rig(f.sim, w.seat, Level::Hard, std::make_unique<StandardBot>(plan));
                auto& bot = rig.as<StandardBot>();
                GateGoalTask::Params gp = bot.gate_goals().params();
                gp.goals_max = 0;                                                            // (the gate goals are exhausted at their first look)
                bot.gate_goals_for_labs().set_params(gp);
                rig.run(600);
                ASSERT_TRUE(bot.gate_goals().exhausted());
                ASSERT_EQ(bot.tactics().field_map, field);
                const bool acted = bot.bombs().defused() + bot.bombs().set_off() > 0 || bot.bombs().busy() || !f.sim.grid().has_bomb_at(bomb);
                ASSERT_EQ(acted, !field);
            }
        }
    } TEST_END();
}
