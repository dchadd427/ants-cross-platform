// AI29: the field breach (docs/BOT_GATES.md, "The field breach"): one ant opens a pile that lies beyond a field of bombs, a fire wall and water (plan_field, next_field_step).
// The worlds are made by hand and drawn for every seat, straight and transposed (Drawing, b41_helpers.hpp); POPcOrN, a map of the library, is the one the rule was found on.
//
//   AI29.1   the plan of the field world: the route, the work in its order (bombs, bridge, fire), the sources on the pile's island, the bombs round the pile; no plan where the pile is free or
//            where a wall of one kind stands alone (the gate goals' own)
//   AI29.2   a Bomber on the hill's side leaves it with a hop of ONE bomb onto bomb-free ground in the field; a hurt one heals first; without that ground the hop is a chain
//   AI29.3   a Bomber in the field digs: it defuses a side of its own tile (N, S, W or E) and steps one tile at a time while bombs lie next to its way; over safe ground it walks
//   AI29.4   a Swimmer bridges the fire way's water from the land beside it; a Fire Ant from the island puts the route's fire cell out from a side
//   AI29.5   the Fire Ant's way leaves its power-up over water where the power-up that the ant leaves behind (on a free land tile beside it, the engine draws which) would shut a way
//            over land
//   AI29.6   the bombs of the seat's own colour that lay on the map at the start (many levels give the map's bombs to team 0) are work like any other; one planted since is a wall
//   AI29.12  an ant that took a power-up never takes one at the end of a corridor (the power-up it leaves would shut it in); an ant of the level's kind does
#include <algorithm>
#include <cstdlib>

#include "ai_test.hpp"
#include "b41_helpers.hpp"
#include "ants_ai/field_plan.hpp"
#include "ants_sim/movement_tables.hpp"

using namespace ai_test;
using namespace ants;
using namespace ants::ai;
using namespace b41;

namespace {

// The field world of seat 0 (its hill at (4, 4)), drawn for a seat: a fire wall across row 12 (x 0 .. 30, rock beyond to the field's edge), a field of map bombs below it (rows 13 .. 33,
// x 0 .. 30, rock beyond), with bomb-free ground at (20, 14) that a hop of one bomb from the hill's side reaches. In the field a lake ring (rows and columns 21 .. 26 of x 12 .. 17) round an
// island of 4 x 4 (x 13 .. 16, y 22 .. 25) that holds the pile (a crackers pile on (14 .. 15, 23 .. 24)), a Swimmer power-up (13, 25) and a Fire power-up (16, 25) on its south corners, bombs
// on the rest of its ring but the north-west corner (13, 22) and the two tiles between the power-ups. A Bomber power-up at home (12, 8) and a worker (8, 10). (A power-up placed by hand is
// solid, as one an ant leaves: no walk passes it and no flight lands on it. The power-ups of a map file need not be: POPcOrN.)
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

    TEST_CASE("AI29.6 The Bombs Of The Seat's Own Colour That Lay On The Map At The Start Are Work Like Any Other; One Planted Since Is A Wall (Every Seat, Straight And Transposed)") {
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
}
