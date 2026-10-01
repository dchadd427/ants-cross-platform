// Tests of the analysis of the map (AI1.8 .. AI1.20). MapInfo duplicates a few rules of the engine (which tile a walking ant may stand on, what a step costs, when the path
// finder gives up, how a pile is clicked); every duplicated rule has its conformance test here, against the engine's own path manager (PATHMGR) or the engine's own ants:
//
//   AI1.8   hill geometry against the flags the engine puts on the grid
//   AI1.9   the flood fill (components) against PATHMGR on 300 random (start, goal) pairs per shipped map, and the costs against the path the engine found
//   AI1.10  the same around the hills (mounds, ramps, queue rows of every team)
//   AI1.11  walkable() against the engine's acceptance of a goal, tile by tile
//   AI1.12  the step weights, by hand and by the walking time they stand for
//   AI1.13  the cost limit of the path finder, on a snake of corridors
//   AI1.14  the weights again, on random terrain
//   AI1.15  reachable points per map, pinned
//   AI1.16  components and stranded ants, the power-up list
//   AI1.17  the click tile of a pile starts a harvest loop, the trip model against the measured solo cycle
//   AI1.18  a hand-made world: piles behind a wall, the approach of a pile that shrinks
//   AI1.19  a pile beyond the path finder's limit is not offered
//   AI1.20  two objects on one anchor: which one is clicked, which one is bitten, whose points
#include "ai_test.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>

#include "ants_ai/map_info.hpp"
#include "ants_ai/rng.hpp"

using namespace ai_test;
using namespace ants;
using namespace ants::ai;

namespace {

constexpr const char* kMaps[] = {"TINY", "SMALL", "MEDIUM", "GAUNTLET", "TREASURE", "ISLANDS"};

void clear_ants(sim::SimulationEngine& sim) {
    std::vector<uint32_t> ids;
    for (const sim::AntSnapshot& a : sim.get_world_state().ants) ids.push_back(a.id);
    for (uint32_t id : ids) sim.kill_unit(id);
}

struct Walk {
    bool delivered{false};
    bool failed{false};
    std::vector<TileCoord> path;
    uint32_t ant{0};
};

// Puts a worker of `team` on `from` (the engine has no other ant), gives it the group move to `to` and waits until the path manager has answered: a path (the waypoints the engine
// gave the ant: start ... goal) or a failure
Walk order_and_wait(sim::SimulationEngine& sim, uint8_t team, TileCoord from, TileCoord to, uint32_t max_ticks = 80) {
    Walk w;
    sim.set_locomotion_trace_enabled(true);
    w.ant = sim.spawn_unit(team, sim::AntType::Worker, from);
    Command c;
    c.type = CommandType::GroupMove;
    c.issuer = team;
    c.tile_x = static_cast<int16_t>(to.x);
    c.tile_y = static_cast<int16_t>(to.y);
    c.ants = {w.ant};
    sim.apply_command(c);
    size_t seen = 0;
    for (uint32_t t = 0; t < max_ticks && !w.delivered && !w.failed; ++t) {
        sim.tick();
        const auto& trace = sim.locomotion_trace();
        for (; seen < trace.size(); ++seen) {
            if (trace[seen].ant_id != w.ant) continue;
            if (trace[seen].kind == sim::LocoTraceEvent::Kind::PathDelivered) w.delivered = true;
            if (trace[seen].kind == sim::LocoTraceEvent::Kind::PathFailed) w.failed = true;
        }
    }
    if (w.delivered) w.path = sim.get_unit(w.ant).waypoints;
    return w;
}

int64_t path_cost(const sim::Grid& grid, const std::vector<TileCoord>& path) {
    int64_t sum = 0;
    for (size_t i = 1; i < path.size(); ++i) sum += MapInfo::step_cost(grid, path[i - 1], path[i]);
    return sum;
}

struct Tally {
    int pairs{0};
    int reachable{0};
    int flood_vs_engine{0};            // disagreements between the flood fill and the engine's answer
    int limit_vs_engine{0};            // disagreements between "cost below the limit" and the engine's answer
    int cheaper{0};                    // the engine's path costs LESS than the optimum of MapInfo: its weights or its tiles differ from the engine's
    int suboptimal{0};                 // the engine's path costs more than the optimum (the original's path finder is not exact: it has no decrease-key)
    double worst{1.0};                 // the largest ratio path cost / optimum
    int odd{0};                        // a delivered path that does not start at the start or end at the goal
};

// `window` 0: anywhere on the map; else within `window` tiles around the origin of a hill
Tally play_pairs(const std::string& map, int count, uint64_t seed, int window) {
    Tally t;
    BotRng rng(seed);
    sim::SimulationEngine base;
    start_match(base, map, 1, 0x0F);
    const MapInfo mi(base);
    while (t.pairs < count) {
        sim::SimulationEngine sim;
        start_match(sim, map, 1, 0x0F);
        clear_ants(sim);
        const sim::Grid& g = sim.grid();
        const uint8_t team = static_cast<uint8_t>(rng.below(4));
        const auto pick = [&](TileCoord around) {
            for (int tries = 0; tries < 10000; ++tries) {
                TileCoord p;
                if (window == 0) {
                    p = TileCoord{static_cast<int32_t>(rng.below(g.width())), static_cast<int32_t>(rng.below(g.height()))};
                } else {
                    p = TileCoord{around.x - window + static_cast<int32_t>(rng.below(static_cast<uint32_t>(2 * window + 5))), around.y - window + static_cast<int32_t>(rng.below(static_cast<uint32_t>(2 * window + 5)))};
                }
                if (MapInfo::walkable(g, team, p)) return p;
            }
            return TileCoord{-1, -1};
        };
        const TileCoord around = window == 0 ? TileCoord{0, 0} : mi.hill(static_cast<uint8_t>(rng.below(4))).origin;
        const TileCoord from = pick(around);
        TileCoord to = pick(around);
        if (from.x < 0 || to.x < 0 || from == to) continue;
        const bool flood = mi.component(team, from) == mi.component(team, to);
        const std::vector<int32_t> field = MapInfo::cost_field(g, team, from);
        const int32_t optimum = field[static_cast<size_t>(to.y) * g.width() + static_cast<size_t>(to.x)];
        const bool within_limit = optimum >= 0 && optimum < MapInfo::kPathCostLimit;
        const Walk w = order_and_wait(sim, team, from, to);
        ++t.pairs;
        if (!w.delivered && !w.failed) {
            std::cout << "    no answer from the path manager: " << map << " team " << int(team) << " (" << from.x << "," << from.y << ") -> (" << to.x << "," << to.y << ")\n";
            ++t.flood_vs_engine;
            continue;
        }
        t.reachable += w.delivered ? 1 : 0;
        if (w.delivered != flood) {
            ++t.flood_vs_engine;
            std::cout << "    flood fill says " << flood << ", the engine " << w.delivered << ": " << map << " team " << int(team) << " (" << from.x << "," << from.y << ") -> (" << to.x << "," << to.y << ")\n";
        }
        if (w.delivered != within_limit) {
            ++t.limit_vs_engine;
            std::cout << "    cost " << optimum << " says " << within_limit << ", the engine " << w.delivered << ": " << map << " team " << int(team) << "\n";
        }
        if (!w.delivered) continue;
        if (w.path.size() < 2 || w.path.front() != from || w.path.back() != to) {
            ++t.odd;
            continue;
        }
        const int64_t pc = path_cost(g, w.path);
        if (optimum >= 0 && pc < optimum) ++t.cheaper;
        if (optimum > 0 && pc > optimum) {
            ++t.suboptimal;
            t.worst = std::max(t.worst, static_cast<double>(pc) / static_cast<double>(optimum));
        }
    }
    return t;
}

}  // namespace

void run_map_tests() {
    TEST_CASE("AI1.8 Hills: The Geometry Is The Engine's, On Every Shipped Map And For Every Team (Ramp, Hole, Queue Tile, Raid Tile, Waiting Spot); A Seat That Does Not Play Has No Hill") {
        for (const char* name : kMaps) {
            sim::SimulationEngine sim;
            start_match(sim, name, 1, 0x0F);
            const MapInfo mi(sim);
            const sim::Grid& g = sim.grid();
            ASSERT_EQ(mi.width(), static_cast<int>(g.width()));
            ASSERT_EQ(mi.height(), static_cast<int>(g.height()));
            for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
                const assets::AnthillSpawn* ah = g.find_anthill(t);
                ASSERT_TRUE(ah != nullptr);
                const HillInfo& h = mi.hill(t);
                const int32_t bx = static_cast<int32_t>(ah->x);
                const int32_t by = static_cast<int32_t>(ah->y);
                ASSERT_TRUE(h.present);
                ASSERT_TRUE(h.origin == tc(bx, by) && h.mouth == tc(bx + 1, by) && h.entrance == tc(bx + 1, by + 1));
                ASSERT_TRUE(h.queue == tc(bx + 1, by - 1) && h.raid == tc(bx + 3, by + 2) && h.tile42 == tc(bx + 4, by + 4));
                // the same tiles as the engine marks them on the grid
                ASSERT_TRUE(g.get_cell(h.mouth).is_base_hole && g.get_cell(h.entrance).is_base_hole);
                ASSERT_TRUE(g.get_cell(h.raid).is_thief_only);
                for (int dx = 0; dx <= 2; ++dx) ASSERT_EQ(g.get_cell(TileCoord{bx + dx, by - 1}).base_owner_team, t);        // the three queue tiles belong to the hill's team
                // an ant walks on the queue tile of its own hill and the waiting spot, never on the ramp, the hole or the raid tile; another team's ants never on the queue row
                ASSERT_TRUE(MapInfo::walkable(g, t, h.queue) && MapInfo::walkable(g, t, h.tile42));
                ASSERT_FALSE(MapInfo::walkable(g, t, h.mouth) || MapInfo::walkable(g, t, h.entrance) || MapInfo::walkable(g, t, h.raid));
                for (uint8_t other = 0; other < sim::MAX_PLAYERS; ++other) {
                    if (other == t) continue;
                    ASSERT_FALSE(MapInfo::walkable(g, other, h.queue));
                    ASSERT_FALSE(MapInfo::walkable(g, other, {bx, by - 1}));
                    ASSERT_FALSE(MapInfo::walkable(g, other, {bx + 2, by - 1}));
                }
                for (int dy = 0; dy < 4; ++dy) {
                    for (int dx = 0; dx < 4; ++dx) ASSERT_FALSE(MapInfo::walkable(g, t, {bx + dx, by + dy}));          // the whole mound
                }
                ASSERT_FALSE(mi.cost_field_of(t).empty());
                ASSERT_EQ(mi.walking_cost(t, h.queue), 0);
            }
            ASSERT_FALSE(mi.hill(9).present);
        }
        sim::SimulationEngine two;
        start_match(two, "TINY", 1, 0x05);                                                      // seats 0 and 2 only
        const MapInfo m2(two);
        ASSERT_TRUE(m2.hill(0).present && m2.hill(2).present && !m2.hill(1).present && !m2.hill(3).present);
        ASSERT_TRUE(m2.cost_field_of(1).empty() && !m2.cost_field_of(0).empty());
        ASSERT_EQ(m2.walking_cost(1, tc(10, 10)), -1);
        ASSERT_EQ(m2.hill_component(1), -1);
        // the queue tile of another team's hill is closed to an ant, open to the hill's own
        ASSERT_FALSE(MapInfo::walkable(two.grid(), 0, m2.hill(2).queue));
        ASSERT_TRUE(MapInfo::walkable(two.grid(), 2, m2.hill(2).queue));
    } TEST_END();

    TEST_CASE("AI1.9 Flood Fill Against PATHMGR: 300 Random Pairs Of Tiles On Each Of The Six Shipped Maps, A Fresh Engine Per Pair; The Engine Finds A Path Exactly When The Tiles Are In One Component, And Never One Cheaper Than MapInfo's Optimum") {
        for (const char* name : kMaps) {
            const Tally t = play_pairs(name, 300, 0x5EED0000u + static_cast<uint64_t>(name[0]), 0);
            ASSERT_EQ(t.pairs, 300);
            ASSERT_EQ(t.flood_vs_engine, 0);                                                    // the components are exact on the shipped maps
            ASSERT_EQ(t.limit_vs_engine, 0);                                                    // and so is "optimum below the path finder's limit"
            ASSERT_EQ(t.cheaper, 0);                                                            // MapInfo's weights are the engine's: no engine path is cheaper than the optimum
            ASSERT_EQ(t.odd, 0);
            ASSERT_TRUE(t.suboptimal * 10 <= t.reachable);                                      // the original's search is not exact (no decrease-key): a few paths cost more, never by much
            ASSERT_TRUE(t.worst <= 1.15);
            if (std::string(name) == "ISLANDS") ASSERT_TRUE(t.reachable < 150 && t.reachable > 5);      // most pairs are on different islands, and the engine refuses them all
            else if (std::string(name) == "SMALL") ASSERT_TRUE(t.reachable < 300 && t.reachable > 250);  // a lake
            else ASSERT_TRUE(t.reachable > 250);
        }
    } TEST_END();

    TEST_CASE("AI1.10 The Same Around The Hills: 150 Pairs In The Neighbourhood Of A Hill On Each Map (Mounds, Ramps, The Queue Rows Of Every Team: Own Rows Open, Other Teams' Rows Closed)") {
        for (const char* name : kMaps) {
            const Tally t = play_pairs(name, 150, 0xB111u + static_cast<uint64_t>(name[0]), 6);
            ASSERT_EQ(t.pairs, 150);
            ASSERT_EQ(t.flood_vs_engine, 0);
            ASSERT_EQ(t.limit_vs_engine, 0);
            ASSERT_EQ(t.cheaper, 0);
            ASSERT_EQ(t.odd, 0);
            ASSERT_TRUE(t.suboptimal * 5 <= t.reachable);
            ASSERT_TRUE(t.worst <= 1.15);
        }
    } TEST_END();

    TEST_CASE("AI1.11 walkable() Against The Engine's Acceptance Of A Goal, Tile By Tile: Around Every Hill And At Random, For Every Team; A Goal The Engine Cannot Enter Is Moved Or Refused") {
        size_t probes = 0;
        size_t refused_tiles = 0;
        for (const char* name : kMaps) {
            sim::SimulationEngine base;
            start_match(base, name, 1, 0x0F);
            const MapInfo mi(base);
            BotRng rng(0xFACE + static_cast<uint64_t>(name[0]));
            std::vector<std::pair<uint8_t, TileCoord>> cases;
            for (uint8_t hill = 0; hill < sim::MAX_PLAYERS; ++hill) {
                for (int dy = -2; dy <= 5; ++dy) {
                    for (int dx = -2; dx <= 5; ++dx) {
                        const TileCoord t{mi.hill(hill).origin.x + dx, mi.hill(hill).origin.y + dy};
                        if (!base.grid().in_bounds(t)) continue;
                        for (uint8_t team = 0; team < sim::MAX_PLAYERS; ++team) cases.emplace_back(team, t);
                    }
                }
            }
            for (int i = 0; i < 200; ++i) {
                cases.emplace_back(static_cast<uint8_t>(rng.below(4)), TileCoord{static_cast<int32_t>(rng.below(base.grid().width())), static_cast<int32_t>(rng.below(base.grid().height()))});
            }
            for (const auto& [team, tile] : cases) {
                const sim::Grid& bg = base.grid();
                const sim::TileCell& cell = bg.get_cell(tile);
                // an order onto the team's own mound is the way home, and onto a power-up, a pile, a lunchbox or a fire wall is a pick-up, a harvest or a walk to an object: other rules
                bool own_mound = false;
                for (const assets::AnthillSpawn& ah : bg.anthills()) {
                    own_mound = own_mound || (ah.team_id == team && tile.x >= static_cast<int32_t>(ah.x) && tile.x <= static_cast<int32_t>(ah.x) + 3 && tile.y >= static_cast<int32_t>(ah.y) &&
                                              tile.y <= static_cast<int32_t>(ah.y) + 3);
                }
                if (own_mound || cell.has_powerup() || cell.has_food() || cell.has_lunchbox() || cell.has_fire()) continue;
                // the start: a tile next to it that the ant may stand on, in the same component as the goal when the goal is walkable
                const bool walkable = MapInfo::walkable(bg, team, tile);
                TileCoord from{-1, -1};
                for (int dy = -1; dy <= 1 && from.x < 0; ++dy) {
                    for (int dx = -1; dx <= 1 && from.x < 0; ++dx) {
                        const TileCoord n{tile.x + dx, tile.y + dy};
                        if ((dx != 0 || dy != 0) && MapInfo::walkable(bg, team, n) && (!walkable || mi.component(team, n) == mi.component(team, tile))) from = n;
                    }
                }
                if (from.x < 0) continue;
                sim::SimulationEngine sim;
                start_match(sim, name, 1, 0x0F);
                clear_ants(sim);
                const Walk w = order_and_wait(sim, team, from, tile);
                const bool accepted = w.delivered && !w.path.empty() && w.path.back() == tile;
                ++probes;
                refused_tiles += walkable ? 0u : 1u;
                if (accepted != walkable) {
                    std::cout << "    " << name << " team " << int(team) << " tile (" << tile.x << "," << tile.y << "): walkable " << walkable << ", the engine " << accepted << "\n";
                }
                ASSERT_EQ(accepted, walkable);
            }
        }
        ASSERT_TRUE(probes > 3500);
        ASSERT_TRUE(refused_tiles > 1000);                                                      // plenty of tiles the engine refuses: mounds of other hills, water, obstacles, other teams' queue rows
    } TEST_END();

    TEST_CASE("AI1.12 The Step Weights: By Hand (Grass 20, Sand 16, Mud 48, Dirt 24; A Diagonal Step 1.4 Times, Truncated; Halved) And By Walking Time (Every Unit Of Cost Is 0.4 Tick On Every Terrain)") {
        sim::SimulationEngine sim;
        sim.init_test_world(60, 60, 5, 720000);
        sim::Grid& g = sim.grid_mut();
        const struct { uint8_t terrain; int32_t orth; int32_t diag; } same[] = {{0, 20, 28}, {1, 16, 22}, {3, 48, 67}, {4, 24, 33}};
        for (const auto& s : same) {
            g.set_terrain_class(10, 10, s.terrain);
            g.set_terrain_class(11, 10, s.terrain);
            g.set_terrain_class(11, 11, s.terrain);
            ASSERT_EQ(MapInfo::step_cost(g, {10, 10}, {11, 10}), static_cast<uint32_t>(s.orth));
            ASSERT_EQ(MapInfo::step_cost(g, {11, 10}, {10, 10}), static_cast<uint32_t>(s.orth));
            ASSERT_EQ(MapInfo::step_cost(g, {10, 10}, {11, 11}), static_cast<uint32_t>(s.diag));
            g.set_terrain_class(10, 10, 0);
            g.set_terrain_class(11, 10, 0);
            g.set_terrain_class(11, 11, 0);
        }
        // mixed: the two weights added and halved
        g.set_terrain_class(11, 10, 3);
        ASSERT_EQ(MapInfo::step_cost(g, {10, 10}, {11, 10}), 34u);                              // (20 + 48) / 2
        ASSERT_EQ(MapInfo::step_cost(g, {10, 11}, {11, 10}), 47u);                              // (68 * 1.4 = 95.2 -> 95) / 2
        g.set_terrain_class(11, 10, 1);
        ASSERT_EQ(MapInfo::step_cost(g, {10, 10}, {11, 10}), 18u);
        g.set_terrain_class(11, 10, 4);
        ASSERT_EQ(MapInfo::step_cost(g, {10, 10}, {11, 10}), 22u);
        ASSERT_EQ(MapInfo::step_cost(g, {10, 11}, {11, 10}), 30u);                              // (44 * 1.4 = 61.6 -> 61) / 2
        // the table is the engine's: every pair of terrain classes against movement::terrain_step_weight
        const uint8_t classes[] = {0, 1, 3, 4};
        for (uint8_t a : classes) {
            for (uint8_t b : classes) {
                g.set_terrain_class(20, 20, a);
                g.set_terrain_class(21, 20, b);
                ASSERT_EQ(MapInfo::step_cost(g, {20, 20}, {21, 20}), (sim::movement::terrain_step_weight(a, false) + sim::movement::terrain_step_weight(b, false)) >> 1);
            }
        }
        // the walking time: a worker walks a straight row of 20 tiles; the engine's A* needs the cost, the clock needs 0.4 tick per unit of it
        struct Terrain { uint8_t cls; const char* name; };
        for (const Terrain& tr : {Terrain{0, "grass"}, Terrain{1, "sand"}, Terrain{4, "dirt"}, Terrain{3, "mud"}}) {
            sim::SimulationEngine w;
            w.init_test_world(60, 60, 6, 720000);
            for (int y = 0; y < 60; ++y) {
                for (int x = 0; x < 60; ++x) w.grid_mut().set_terrain_class(x, y, tr.cls);
            }
            w.set_locomotion_trace_enabled(true);
            const uint32_t ant = w.spawn_unit(0, sim::AntType::Worker, TileCoord{10, 30});
            Command c;
            c.type = CommandType::GroupMove;
            c.issuer = 0;
            c.tile_x = 30;
            c.tile_y = 30;
            c.ants = {ant};
            w.apply_command(c);
            const std::vector<int32_t> field = MapInfo::cost_field(w.grid(), 0, TileCoord{10, 30});
            const int32_t cost = field[30 * 60 + 30];
            ASSERT_EQ(cost, 20 * static_cast<int32_t>(sim::movement::terrain_step_weight(tr.cls, false)));
            uint32_t delivered_at = 0;
            uint32_t arrived_at = 0;
            for (uint32_t t = 1; t < 1200 && arrived_at == 0; ++t) {
                w.tick();
                if (delivered_at == 0) {
                    for (const auto& ev : w.locomotion_trace()) {
                        if (ev.ant_id == ant && ev.kind == sim::LocoTraceEvent::Kind::PathDelivered) delivered_at = t;
                    }
                }
                const sim::AntUnit& u = w.get_unit(ant);
                if (delivered_at != 0 && u.pixel_x / 32 == 30 && u.pixel_y / 32 == 30) arrived_at = t;
            }
            ASSERT_TRUE(delivered_at != 0 && arrived_at != 0);
            const double walked = static_cast<double>(arrived_at - delivered_at);
            const double predicted = static_cast<double>(MapInfo::walking_ticks(cost));
                        ASSERT_TRUE(std::abs(walked - predicted) <= predicted * 0.08 + 4);                  // within 8 percent and the first step (mud walks 7 percent faster than its weight says)
        }
        ASSERT_EQ(MapInfo::walking_ticks(20), 8);
        ASSERT_EQ(MapInfo::trip_ticks_for_cost(-1), -1);
        ASSERT_EQ(MapInfo::trip_ticks_for_cost(100), 2 * 100 * 2 / 5 + 70);
    } TEST_END();

    TEST_CASE("AI1.13 The Path Finder's Limit: On A Snake Of Corridors A Goal At A Cost Just Below 8,000 Is Found And One At 8,000 Or More Is Not, Though Both Are In One Component; MapInfo Draws The Same Line") {
        // even rows are corridors, odd rows walls with one gap at the east end (rows 3, 7, ...) or the west end (rows 5, 9, ...)
        const auto snake = [](sim::SimulationEngine& sim) {
            sim.init_test_world(60, 60, 7, 720000);
            for (int y = 3; y <= 57; y += 2) {
                for (int x = 0; x < 60; ++x) {
                    const bool gap = (y % 4 == 3) ? x == 59 : x == 0;
                    if (!gap) sim.set_terrain(x, y, sim::TERRAIN_OBSTACLE);
                }
            }
        };
        sim::SimulationEngine sim;
        snake(sim);
        const MapInfo mi(sim);
        const sim::Grid& g = sim.grid();
        const TileCoord start{0, 2};
        const std::vector<int32_t> field = MapInfo::cost_field(g, 0, start);
        TileCoord low{-1, -1};
        TileCoord high{-1, -1};
        int32_t best_low = -1;
        int32_t best_high = 1 << 30;
        for (int y = 0; y < 60; ++y) {
            for (int x = 0; x < 60; ++x) {
                const int32_t c = field[static_cast<size_t>(y) * 60 + static_cast<size_t>(x)];
                if (c >= 0 && c < MapInfo::kPathCostLimit && c > best_low) { best_low = c; low = {x, y}; }
                if (c >= MapInfo::kPathCostLimit && c < best_high) { best_high = c; high = {x, y}; }
            }
        }
        ASSERT_TRUE(low.x >= 0 && high.x >= 0);
        ASSERT_TRUE(best_low >= 7900 && best_low < 8000);
        ASSERT_TRUE(best_high >= 8000 && best_high <= 8100);
        ASSERT_EQ(mi.component(0, start), mi.component(0, low));                                // one component: the flood fill cannot tell the two goals apart ...
        ASSERT_EQ(mi.component(0, start), mi.component(0, high));
        sim::SimulationEngine a;
        snake(a);
        const Walk found = order_and_wait(a, 0, start, low, 200);
        ASSERT_TRUE(found.delivered && !found.failed);                                          // ... the engine's finder finds the one below the limit
        ASSERT_EQ(path_cost(a.grid(), found.path), best_low);                                   // and its path is the optimum (on a snake there is only one way)
        sim::SimulationEngine b;
        snake(b);
        const Walk lost = order_and_wait(b, 0, start, high, 200);
        ASSERT_TRUE(lost.failed && !lost.delivered);                                            // ... and gives up on the one beyond it
    } TEST_END();

    TEST_CASE("AI1.14 The Weights On Random Terrain: 300 Pairs On A World Of Sand, Dirt, Mud And Grass Patches With Lakes And Rocks; The Engine's Path Is Never Cheaper Than MapInfo's Optimum And Nearly Always Equal To It") {
        BotRng rng(0xC0FFEE);
        const auto build = [&](sim::SimulationEngine& sim, uint64_t seed) {
            sim.init_test_world(50, 50, 9, 720000);
            BotRng wr(seed);
            for (int patch = 0; patch < 60; ++patch) {                                           // blobs of one terrain class
                const int cx = static_cast<int>(wr.below(50));
                const int cy = static_cast<int>(wr.below(50));
                const int r = 2 + static_cast<int>(wr.below(5));
                const uint8_t cls = static_cast<uint8_t>(std::array<uint8_t, 6>{1, 3, 4, 3, 2, 4}[wr.below(6)]);
                for (int y = std::max(1, cy - r); y <= std::min(49, cy + r); ++y) {
                    for (int x = std::max(0, cx - r); x <= std::min(49, cx + r); ++x) {
                        if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r) sim.grid_mut().set_terrain_class(x, y, cls);
                    }
                }
            }
            for (int rock = 0; rock < 40; ++rock) sim.set_terrain(static_cast<int32_t>(wr.below(50)), 1 + static_cast<int32_t>(wr.below(49)), sim::TERRAIN_OBSTACLE);
        };
        int pairs = 0;
        int reachable = 0;
        int cheaper = 0;
        int suboptimal = 0;
        int disagree = 0;
        std::map<int, int> classes_on_paths;
        double worst_ratio = 1.0;
        sim::SimulationEngine base;
        build(base, 77);
        const MapInfo mi(base.grid());
        while (pairs < 300) {
            sim::SimulationEngine sim;
            build(sim, 77);
            const sim::Grid& g = sim.grid();
            TileCoord from{-1, -1};
            TileCoord to{-1, -1};
            for (int tries = 0; tries < 10000 && from.x < 0; ++tries) {
                const TileCoord p{static_cast<int32_t>(rng.below(50)), 1 + static_cast<int32_t>(rng.below(49))};
                if (MapInfo::walkable(g, 0, p)) from = p;
            }
            for (int tries = 0; tries < 10000 && to.x < 0; ++tries) {
                const TileCoord p{static_cast<int32_t>(rng.below(50)), 1 + static_cast<int32_t>(rng.below(49))};
                if (MapInfo::walkable(g, 0, p) && p != from) to = p;
            }
            ASSERT_TRUE(from.x >= 0 && to.x >= 0);
            ++pairs;
            const int32_t optimum = MapInfo::cost_field(g, 0, from)[static_cast<size_t>(to.y) * 50 + static_cast<size_t>(to.x)];
            const Walk w = order_and_wait(sim, 0, from, to);
            const bool flood = mi.component(0, from) == mi.component(0, to);
            if (w.delivered != flood) ++disagree;
            if (!w.delivered) continue;
            ++reachable;
            for (const TileCoord& t : w.path) ++classes_on_paths[g.terrain_class_at(t)];
            const int64_t pc = path_cost(g, w.path);
            if (pc < optimum) ++cheaper;
            if (pc > optimum) {
                ++suboptimal;
                worst_ratio = std::max(worst_ratio, static_cast<double>(pc) / static_cast<double>(optimum));
            }
        }
        ASSERT_EQ(disagree, 0);
        ASSERT_EQ(cheaper, 0);
        ASSERT_TRUE(reachable > 200);
        ASSERT_TRUE(suboptimal * 4 <= reachable);                                                // the original's search is not exact: about one path in eight costs a little more ...
        ASSERT_TRUE(worst_ratio <= 1.05);                                                        // ... never by more than a few percent
        ASSERT_TRUE(classes_on_paths[0] > 0 && classes_on_paths[1] > 0 && classes_on_paths[3] > 0 && classes_on_paths[4] > 0);       // the paths crossed every walkable class
    } TEST_END();

    TEST_CASE("AI1.15 Reachable Points Per Map Are Pinned: TINY 4800 Of 4800, SMALL 3000 Of 4000, MEDIUM 4900 Of 4900, GAUNTLET 1500 Of 1800, ISLANDS 0 Of 5600, TREASURE 8850 Of 10950; The Same For Every Hill") {
        struct Row { const char* map; uint32_t reachable; uint32_t total; size_t piles; std::vector<uint32_t> unreachable; };
        const Row rows[] = {
            {"TINY", 4800, 4800, 14, {}},
            {"SMALL", 3000, 4000, 5, {0}},
            {"MEDIUM", 4900, 4900, 4, {}},
            {"GAUNTLET", 1500, 1800, 2, {1}},
            {"ISLANDS", 0, 5600, 10, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9}},
            {"TREASURE", 8850, 10950, 18, {7, 8, 9, 10, 11}},
        };
        for (const Row& r : rows) {
            sim::SimulationEngine sim;
            start_match(sim, r.map, 1, 0x0F);
            const MapInfo mi(sim);
            ASSERT_EQ(mi.piles().size(), r.piles);
            ASSERT_EQ(mi.total_points(), r.total);
            ASSERT_EQ(mi.reachable_points(), r.reachable);
            for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) ASSERT_EQ(mi.reachable_points(t), r.reachable);
            for (const PileInfo& p : mi.piles()) {
                const bool listed = std::find(r.unreachable.begin(), r.unreachable.end(), p.index) != r.unreachable.end();
                for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
                    ASSERT_EQ(p.approach[t].reachable(), !listed);
                    ASSERT_EQ(mi.trip_ticks(t, p.index) >= 0, !listed);
                }
                ASSERT_TRUE(p.units == sim.grid().food_objects()[p.index].units && p.value == sim.grid().food_objects()[p.index].value);
                ASSERT_TRUE(p.anchor == tc(sim.grid().food_objects()[p.index].col, sim.grid().food_objects()[p.index].row));
            }
            // a click tile is a cell of the pile itself, as the engine attributes it
            for (const PileInfo& p : mi.piles()) {
                for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
                    if (!p.approach[t].reachable()) continue;
                    ASSERT_EQ(sim.grid().food_object_at_cell(p.approach[t].click), static_cast<int32_t>(p.index));
                    ASSERT_TRUE(std::find(p.cells.begin(), p.cells.end(), p.approach[t].click) != p.cells.end());
                }
            }
        }
        // duplicates of an anchor on TREASURE: the last object of the table owns the cells, the first one is bitten
        sim::SimulationEngine tr;
        start_match(tr, "TREASURE", 1, 0x0F);
        const MapInfo tm(tr);
        ASSERT_TRUE(tm.piles()[7].cells.empty() && tm.piles()[8].cells.empty() && tm.piles()[10].cells.empty());
        ASSERT_TRUE(!tm.piles()[9].cells.empty() && !tm.piles()[11].cells.empty());
        ASSERT_TRUE(tm.piles()[9].bite_index == 7 && tm.piles()[10].bite_index == 8 && tm.piles()[11].bite_index == 8 && tm.piles()[7].bite_index == 7);
        ASSERT_TRUE(tm.piles()[9].anchor == tm.piles()[7].anchor && tm.piles()[11].anchor == tm.piles()[8].anchor);
        for (const PileInfo& p : tm.piles()) {
            if (p.index < 7 || p.index > 11) ASSERT_EQ(p.bite_index, p.index);
        }
    } TEST_END();

    TEST_CASE("AI1.16 Components And The Power-Up List: ISLANDS Strands Two Of Every Hill's Eight Ants On Other Islands (21 Components Per Team), Every Other Map Keeps Everyone Together; The Power-Ups Are The Grid's, Pinned By Type And Reach") {
        for (const char* name : kMaps) {
            sim::SimulationEngine sim;
            start_match(sim, name, 1, 0x0F);
            const MapInfo mi(sim);
            const bool islands = std::string(name) == "ISLANDS";
            std::array<int, 4> stranded{};
            std::array<int, 4> total{};
            for (const sim::AntSnapshot& a : sim.get_world_state().ants) {
                const TileCoord tile{a.tile_x, a.tile_y};
                const int32_t c = mi.ant_component(a.player_id, tile);
                ASSERT_TRUE(c >= 0);
                ++total[a.player_id];
                if (c != mi.hill_component(a.player_id)) ++stranded[a.player_id];
                // an ant is stranded exactly when no pile of the hill's own island is within its reach: the flood fill does not mix islands up
                for (const PileInfo& p : mi.piles()) {
                    if (p.approach[a.player_id].reachable() && !p.cells.empty()) ASSERT_EQ(mi.can_reach_pile(a.player_id, tile, p.index), c == mi.hill_component(a.player_id));
                    if (islands) ASSERT_FALSE(mi.can_reach_pile(a.player_id, tile, p.index));
                }
            }
            for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
                ASSERT_EQ(stranded[t], islands ? 2 : 0);
                ASSERT_EQ(total[t], islands ? 8 : total[0]);
                ASSERT_EQ(mi.component_count(t), islands ? 21 : mi.component_count(0));
                ASSERT_TRUE(mi.hill_component(t) >= 0);
            }
            if (!islands) ASSERT_TRUE(mi.component_count(0) >= 1);
        }
        struct PowerRow { const char* map; size_t count; std::array<int, 6> by_type; int reachable; };
        const PowerRow rows[] = {
            {"TINY", 0, {0, 0, 0, 0, 0, 0}, 0},
            {"SMALL", 2, {0, 0, 0, 0, 0, 2}, 2},
            {"MEDIUM", 0, {0, 0, 0, 0, 0, 0}, 0},
            {"GAUNTLET", 10, {0, 0, 0, 2, 4, 4}, 8},
            {"TREASURE", 20, {0, 4, 4, 4, 4, 4}, 20},
            {"ISLANDS", 40, {0, 12, 16, 0, 0, 12}, 4},
        };
        for (const PowerRow& r : rows) {
            sim::SimulationEngine sim;
            start_match(sim, r.map, 1, 0x0F);
            const MapInfo mi(sim);
            ASSERT_EQ(mi.powerups().size(), r.count);
            std::array<int, 6> by_type{};
            int reachable = 0;
            for (size_t i = 0; i < mi.powerups().size(); ++i) {
                const PowerUpInfo& p = mi.powerups()[i];
                ASSERT_TRUE(sim.grid().has_powerup_at(p.tile));
                ASSERT_EQ(sim.grid().get_powerup_type(p.tile), static_cast<uint8_t>(p.type));
                ++by_type[static_cast<size_t>(p.type)];
                bool any = false;
                for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) any = any || p.approach[t].reachable();
                reachable += any ? 1 : 0;
                if (i > 0) ASSERT_TRUE(mi.powerups()[i - 1].tile.y < p.tile.y || (mi.powerups()[i - 1].tile.y == p.tile.y && mi.powerups()[i - 1].tile.x < p.tile.x));     // reading order
                for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
                    if (p.approach[t].reachable()) ASSERT_TRUE(p.approach[t].click == p.tile);
                }
            }
            ASSERT_TRUE(by_type == r.by_type);
            ASSERT_EQ(reachable, r.reachable);
        }
    } TEST_END();

    TEST_CASE("AI1.17 Clicking The Click Tile Of The Nearest Pile Starts A Harvest Loop That Banks, And The Trip Model (There And Back At 0.4 Tick Per Unit Of Cost, Plus 70) Is Within 25 Percent Of The Measured Cycle Of One Worker, For Every Hill Of The Five Maps With Food On Foot") {
        size_t cases = 0;
        double worst = 0.0;
        for (const char* name : {"TINY", "SMALL", "MEDIUM", "GAUNTLET", "TREASURE"}) {
            sim::SimulationEngine base;
            start_match(base, name, 1, 0x0F);
            const MapInfo mi(base);
            for (uint8_t team = 0; team < sim::MAX_PLAYERS; ++team) {
                // the nearest pile by walking cost (the lowest index on a tie)
                const PileInfo* nearest = nullptr;
                for (const PileInfo& p : mi.piles()) {
                    if (!p.approach[team].reachable()) continue;
                    if (nearest == nullptr || p.approach[team].cost < nearest->approach[team].cost) nearest = &p;
                }
                ASSERT_TRUE(nearest != nullptr);
                // a solo cycle: one worker, nobody else on the field, one command
                sim::SimulationEngine sim;
                start_match(sim, name, 1, 0x0F);
                const Approach& ap = nearest->approach[team];
                uint32_t worker = 0;
                int best = 1 << 30;
                for (const sim::AntSnapshot& a : sim.get_world_state().ants) {
                    if (a.player_id != team || a.type != sim::AntType::Worker) continue;
                    const int d = std::abs(a.tile_x - ap.click.x) + std::abs(a.tile_y - ap.click.y);
                    if (d < best) { best = d; worker = a.id; }
                }
                std::vector<uint32_t> others;
                for (const sim::AntSnapshot& a : sim.get_world_state().ants) {
                    if (a.id != worker) others.push_back(a.id);
                }
                for (uint32_t id : others) sim.kill_unit(id);
                Command c;
                c.type = CommandType::GroupMove;
                c.issuer = team;
                c.tile_x = static_cast<int16_t>(ap.click.x);
                c.tile_y = static_cast<int16_t>(ap.click.y);
                c.ants = {worker};
                ASSERT_TRUE(sim.apply_command(c).accepted());
                std::vector<uint32_t> deposits;
                int32_t last = sim.get_player_score(team);
                const uint32_t limit = static_cast<uint32_t>(std::max(3000, MapInfo::trip_ticks_for_cost(ap.cost) * 9));
                for (uint32_t t = 1; t <= limit && deposits.size() < 7; ++t) {
                    sim.tick();
                    sim.clear_news_events();
                    sim.clear_audio_events();
                    const int32_t s = sim.get_player_score(team);
                    if (s != last) {
                        ASSERT_EQ(s - last, static_cast<int32_t>(nearest->value));          // one unit of the pile it was sent to, banked
                        deposits.push_back(t);
                        last = s;
                    }
                }
                ASSERT_TRUE(deposits.size() >= 5);                                          // one command started a loop that kept going by itself
                std::vector<uint32_t> cycles;
                for (size_t i = 2; i < deposits.size(); ++i) cycles.push_back(deposits[i] - deposits[i - 1]);
                std::sort(cycles.begin(), cycles.end());
                const double measured = static_cast<double>(cycles[cycles.size() / 2]);
                const double model = static_cast<double>(mi.trip_ticks(team, nearest->index));
                const double error = (model - measured) / measured;
                worst = std::max(worst, std::abs(error));
                if (std::abs(error) > 0.25) std::cout << "    " << name << " team " << int(team) << " pile " << nearest->index << ": model " << model << " measured " << measured << "\n";
                ASSERT_TRUE(std::abs(error) <= 0.25);
                ++cases;
            }
        }
        ASSERT_EQ(cases, 20u);
        ASSERT_TRUE(worst > 0.0);
    } TEST_END();

    TEST_CASE("AI1.18 A Hand-Made World: A Pile Behind A Wall Is Not Reachable, One In The Open Costs What Dijkstra Says; A Pile That Shrinks Changes Its Footprint (approach_now), And Is Gone When It Is Eaten") {
        // a wall across the world with a pile behind it
        {
            sim::SimulationEngine sim;
            sim.init_test_world(40, 40, 11, 720000);
            sim.set_anthill(0, TileCoord{2, 2});
            for (int x = 0; x < 40; ++x) sim.set_terrain(x, 20, sim::TERRAIN_OBSTACLE);
            const int32_t open = place_crackers(sim, 12, 10, 25);
            const int32_t behind = place_crackers(sim, 12, 30, 25);
            const MapInfo mi(sim);
            ASSERT_TRUE(mi.piles()[static_cast<size_t>(open)].approach[0].reachable());
            ASSERT_FALSE(mi.piles()[static_cast<size_t>(behind)].approach[0].reachable());
            ASSERT_EQ(mi.reachable_points(), 100u);
            ASSERT_EQ(mi.total_points(), 200u);
            ASSERT_TRUE(mi.component(0, tc(12, 5)) != mi.component(0, tc(12, 30)));
            ASSERT_TRUE(mi.can_reach_pile(0, tc(12, 5), static_cast<uint32_t>(open)));
            ASSERT_FALSE(mi.can_reach_pile(0, tc(12, 5), static_cast<uint32_t>(behind)));
            ASSERT_TRUE(mi.can_reach_pile(0, tc(12, 35), static_cast<uint32_t>(behind)));      // an ant on the far side can reach it: the question is per ant
            // the cost of the open pile, by an independent Bellman-Ford over the same tiles with the weights written out by hand (grass: 20 straight, 28 diagonal)
            std::vector<int32_t> d(40 * 40, 1 << 28);
            d[1 * 40 + 3] = 0;                                                                         // the queue tile of the hill at (2, 2)
            for (int round = 0; round < 100; ++round) {
                for (int y = 1; y < 40; ++y) {
                    for (int x = 0; x < 40; ++x) {
                        if (!MapInfo::walkable(sim.grid(), 0, TileCoord{x, y})) continue;
                        for (int dy = -1; dy <= 1; ++dy) {
                            for (int dx = -1; dx <= 1; ++dx) {
                                const int nx = x + dx;
                                const int ny = y + dy;
                                if ((dx == 0 && dy == 0) || nx < 0 || ny < 1 || nx >= 40 || ny >= 40 || !MapInfo::walkable(sim.grid(), 0, TileCoord{nx, ny})) continue;
                                const int step = (dx != 0 && dy != 0) ? 28 : 20;
                                d[static_cast<size_t>(y * 40 + x)] = std::min(d[static_cast<size_t>(y * 40 + x)], d[static_cast<size_t>(ny * 40 + nx)] + step);
                            }
                        }
                    }
                }
            }
            // the crackers at anchor (12, 10) cover (11..12, 9..10): the cheapest way is a neighbour of one of those cells plus the step onto the cell
            int32_t expect = 1 << 28;
            for (const TileCoord& c : mi.piles()[static_cast<size_t>(open)].cells) {
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = c.x + dx;
                        const int ny = c.y + dy;
                        if ((dx == 0 && dy == 0) || !MapInfo::walkable(sim.grid(), 0, TileCoord{nx, ny})) continue;
                        expect = std::min(expect, d[static_cast<size_t>(ny * 40 + nx)] + ((dx != 0 && dy != 0) ? 28 : 20));
                    }
                }
            }
            ASSERT_TRUE(expect < (1 << 27));
            ASSERT_EQ(mi.piles()[static_cast<size_t>(open)].approach[0].cost, expect);
            ASSERT_EQ(mi.trip_ticks(0, static_cast<uint32_t>(open)), 2 * expect * 2 / 5 + 70);
        }
        // a pile of the TINY map that shrinks: its tile changes with every stage, so the footprint does
        {
            sim::SimulationEngine sim;
            start_match(sim, "TINY", 1, 0x0F);
            const MapInfo mi(sim);
            const uint32_t index = 12;                                                                 // (6, 15): 30 units of 20, 5 cells
            const PileInfo& pile = mi.piles()[index];
            ASSERT_EQ(pile.cells.size(), 5u);
            for (uint8_t team = 0; team < sim::MAX_PLAYERS; ++team) {
                const Approach start = mi.approach_now(sim.grid(), team, index);
                ASSERT_TRUE(start.reachable() && start.cost == pile.approach[team].cost && start.click == pile.approach[team].click);        // at the start both agree
            }
            size_t smallest = pile.cells.size();
            bool click_lost = false;
            for (uint16_t eaten = 1; eaten <= 30; ++eaten) {
                bool changed = false;
                sim.grid_mut().take_food(static_cast<int32_t>(index), 1, changed);
                const sim::FoodObject& o = sim.grid().food_objects()[index];
                if (changed) sim.grid_mut().set_food_tile(TileCoord{o.col, o.row}, o.stage_tile());       // as the end of the bite does (action_system.cpp end_harvest)
                size_t cells_now = 0;
                for (int y = 0; y < 31; ++y) {
                    for (int x = 0; x < 31; ++x) cells_now += sim.grid().food_object_at_cell(TileCoord{x, y}) == static_cast<int32_t>(index) ? 1u : 0u;
                }
                smallest = std::min(smallest, cells_now);
                for (uint8_t team = 0; team < sim::MAX_PLAYERS; ++team) {
                    const Approach now = mi.approach_now(sim.grid(), team, index);
                    if (o.remaining == 0 || cells_now == 0) {
                        ASSERT_FALSE(now.reachable());                                                  // eaten: nothing to walk to
                        continue;
                    }
                    ASSERT_TRUE(now.reachable());
                    ASSERT_EQ(sim.grid().food_object_at_cell(now.click), static_cast<int32_t>(index));   // the click tile is food NOW
                    if (sim.grid().food_object_at_cell(pile.approach[team].click) != static_cast<int32_t>(index)) click_lost = true;
                }
            }
            ASSERT_TRUE(smallest < pile.cells.size());                                                   // the footprint did shrink
            ASSERT_TRUE(click_lost);                                                                     // and the click tile of the start was no longer food for some hill (so approach_now is needed)
            for (uint8_t team = 0; team < sim::MAX_PLAYERS; ++team) ASSERT_FALSE(mi.approach_now(sim.grid(), team, index).reachable());
            ASSERT_FALSE(mi.approach_now(sim.grid(), 0, 9999).reachable());
            ASSERT_FALSE(mi.approach_now(sim.grid(), 9, index).reachable());
        }
    } TEST_END();

    TEST_CASE("AI1.19 A Pile Beyond The Path Finder's Limit Is Not Offered: On A Snake Of Corridors Behind A Hill, The Pile Near The Start Has An Approach, The One At The Far End (Same Component, A Cost Above 8,000) Has None") {
        sim::SimulationEngine tiny;
        start_match(tiny, "TINY", 1, 0x0F);
        const sim::FoodObject model = tiny.grid().food_objects()[0];                                 // a pile of one cell
        sim::SimulationEngine sim;
        sim.init_test_world(60, 60, 12, 720000);
        sim.set_anthill(0, TileCoord{2, 2});                                                          // the mound fills x 2..5, y 2..5; its queue tile is (3, 1)
        for (int y = 2; y <= 58; y += 2) {
            for (int x = 0; x < 60; ++x) {
                if (x >= 2 && x <= 5 && y <= 5) continue;                                             // the mound is a wall already
                const bool east_gap = (y % 4 == 2) && x == 59;
                const bool west_gap = (y % 4 == 0) && x == (y == 4 ? 6 : 0);
                if (!east_gap && !west_gap) sim.set_terrain(x, y, sim::TERRAIN_OBSTACLE);
            }
        }
        const auto pile_at = [&](int32_t col, int32_t row) {
            sim::FoodObject o = model;
            o.col = static_cast<uint16_t>(col);
            o.row = static_cast<uint16_t>(row);
            return sim.grid_mut().add_food_object(o);
        };
        const int32_t near_pile = pile_at(30, 8);                                                     // in a wall row, so that it does not close a corridor
        const int32_t far_pile = pile_at(30, 32);
        const MapInfo mi(sim);
        ASSERT_TRUE(mi.piles()[static_cast<size_t>(near_pile)].cells.size() == 1 && mi.piles()[static_cast<size_t>(far_pile)].cells.size() == 1);
        ASSERT_EQ(mi.component(0, mi.hill(0).queue), mi.component(0, TileCoord{29, 31}));            // one component: a flood fill says the far pile is reachable ...
        ASSERT_TRUE(mi.can_reach_pile(0, mi.hill(0).queue, static_cast<uint32_t>(far_pile)));
        const Approach near_ap = mi.piles()[static_cast<size_t>(near_pile)].approach[0];
        const Approach far_ap = mi.piles()[static_cast<size_t>(far_pile)].approach[0];
        ASSERT_TRUE(near_ap.reachable() && near_ap.cost > 2500 && near_ap.cost < MapInfo::kPathCostLimit);
        ASSERT_FALSE(far_ap.reachable());                                                             // ... but the engine's finder would give up long before it: no approach is offered
        ASSERT_EQ(mi.trip_ticks(0, static_cast<uint32_t>(far_pile)), -1);
        // and the cost field agrees: its value next to the far pile is beyond the limit
        ASSERT_TRUE(mi.walking_cost(0, TileCoord{29, 31}) >= MapInfo::kPathCostLimit);
        ASSERT_EQ(mi.reachable_points(), static_cast<uint32_t>(model.units) * model.value);           // only the near pile counts
        // the engine: an ant at the hill's queue tile is sent to the near pile and finds a path, and is sent next to the far pile and does not
        sim::SimulationEngine a;
        a.init_test_world(60, 60, 12, 720000);
        a.set_anthill(0, TileCoord{2, 2});
        for (int y = 2; y <= 58; y += 2) {
            for (int x = 0; x < 60; ++x) {
                if (x >= 2 && x <= 5 && y <= 5) continue;
                const bool east_gap = (y % 4 == 2) && x == 59;
                const bool west_gap = (y % 4 == 0) && x == (y == 4 ? 6 : 0);
                if (!east_gap && !west_gap) a.set_terrain(x, y, sim::TERRAIN_OBSTACLE);
            }
        }
        const Walk near_walk = order_and_wait(a, 0, mi.hill(0).queue, TileCoord{29, 7}, 300);
        ASSERT_TRUE(near_walk.delivered);
        sim::SimulationEngine b;
        b.init_test_world(60, 60, 12, 720000);
        b.set_anthill(0, TileCoord{2, 2});
        for (int y = 2; y <= 58; y += 2) {
            for (int x = 0; x < 60; ++x) {
                if (x >= 2 && x <= 5 && y <= 5) continue;
                const bool east_gap = (y % 4 == 2) && x == 59;
                const bool west_gap = (y % 4 == 0) && x == (y == 4 ? 6 : 0);
                if (!east_gap && !west_gap) b.set_terrain(x, y, sim::TERRAIN_OBSTACLE);
            }
        }
        const Walk far_walk = order_and_wait(b, 0, mi.hill(0).queue, TileCoord{29, 31}, 300);
        ASSERT_TRUE(far_walk.failed && !far_walk.delivered);
    } TEST_END();

    TEST_CASE("AI1.20 Two Objects On One Anchor: The Last One Owns The Cells And Sets The Points, The First One Loses The Units (The Engine's Own Rule, Seen By An Ant); MapInfo Says Which Is Which") {
        sim::SimulationEngine sim;
        sim.init_test_world(40, 40, 13, 720000);
        sim.set_anthill(0, TileCoord{2, 2});
        const int32_t first = place_crackers(sim, 12, 10, 20);                                         // 4 units of 20
        const int32_t last = place_crackers(sim, 12, 10, 50);                                          // 4 units of 50 on the same anchor: it takes the cells over
        ASSERT_TRUE(first != last);
        const MapInfo mi(sim);
        const PileInfo& a = mi.piles()[static_cast<size_t>(first)];
        const PileInfo& b = mi.piles()[static_cast<size_t>(last)];
        ASSERT_TRUE(a.cells.empty() && !b.cells.empty());                                              // the earlier object cannot be clicked
        ASSERT_TRUE(a.bite_index == static_cast<uint32_t>(first) && b.bite_index == static_cast<uint32_t>(first));
        ASSERT_FALSE(a.approach[0].reachable());
        ASSERT_TRUE(b.approach[0].reachable());
        ASSERT_EQ(mi.reachable_points(), 4u * 50u);                                                    // the bites come out of the first object's 4 units, each is worth the last object's 50
        const uint32_t worker = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{10, 6});
        Command c;
        c.type = CommandType::GroupMove;
        c.issuer = 0;
        c.tile_x = static_cast<int16_t>(b.approach[0].click.x);
        c.tile_y = static_cast<int16_t>(b.approach[0].click.y);
        c.ants = {worker};
        ASSERT_TRUE(sim.apply_command(c).accepted());
        int32_t banked = 0;
        int deposits = 0;
        for (int t = 0; t < 3000 && deposits < 4; ++t) {
            sim.tick();
            sim.clear_news_events();
            sim.clear_audio_events();
            if (sim.get_player_score(0) != banked) {
                ASSERT_EQ(sim.get_player_score(0) - banked, 50);                                        // each bite earns the points of the object the click found: the last
                banked = sim.get_player_score(0);
                ++deposits;
                const uint16_t first_left = sim.grid().food_objects()[static_cast<size_t>(first)].remaining;
                ASSERT_EQ(static_cast<int>(first_left), 4 - deposits - 0);                              // and takes a unit from the first (deposits are banked after the bite)
                ASSERT_EQ(static_cast<int>(sim.grid().food_objects()[static_cast<size_t>(last)].remaining), 4);   // the last is never bitten
            }
        }
        ASSERT_EQ(deposits, 4);
        ASSERT_EQ(banked, 200);
        ASSERT_EQ(static_cast<int>(sim.grid().food_objects()[static_cast<size_t>(first)].remaining), 0);
        ASSERT_EQ(static_cast<int>(sim.grid().food_objects()[static_cast<size_t>(last)].remaining), 4);  // four units that nobody can ever take: the view lists them ...
        const BotView v = BotView::build(sim, 0);
        bool listed_last = false;
        for (const PileView& p : v.piles()) listed_last = listed_last || p.index == static_cast<uint32_t>(last);
        ASSERT_TRUE(listed_last);                                                                      // ... which is why a bot must ask MapInfo (clickable cells, bite_index), not only remaining
        ASSERT_FALSE(MapInfo(sim).piles()[static_cast<size_t>(last)].approach[0].reachable());          // and a fresh analysis agrees: the cells are gone
    } TEST_END();
}
